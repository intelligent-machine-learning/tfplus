/* Copyright 2016 The TensorFlow Authors. All Rights Reserved.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tfplus/kv_variable/kernels/tensor_bundle.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "tensorflow/core/framework/register_types.h"
#include "tensorflow/core/framework/tensor.pb.h"
#include "tensorflow/core/framework/tensor_shape.pb.h"
#include "tensorflow/core/framework/tensor_shape.pb_text.h"
#include "tensorflow/core/framework/types.h"
#include "tensorflow/core/framework/types.pb_text.h"
#include "tensorflow/core/framework/variant.h"
#include "tensorflow/core/framework/variant_op_registry.h"
#include "tensorflow/core/framework/variant_tensor_data.h"
#include "tensorflow/core/framework/versions.h"
#include "tensorflow/core/framework/versions.pb.h"
#include "tensorflow/core/lib/bfloat16/bfloat16.h"
#include "tensorflow/core/lib/core/coding.h"
#include "tensorflow/core/lib/core/errors.h"
#include "tensorflow/core/lib/gtl/map_util.h"
#include "tensorflow/core/lib/gtl/stl_util.h"
#include "tensorflow/core/lib/hash/crc32c.h"
#include "tensorflow/core/lib/io/path.h"
#include "tensorflow/core/lib/io/table_builder.h"
#include "tensorflow/core/lib/random/random.h"
#include "tensorflow/core/lib/strings/stringprintf.h"
#include "tensorflow/core/platform/mutex.h"
#include "tensorflow/core/util/saved_tensor_slice_util.h"
#include "tensorflow/core/util/tensor_slice_util.h"
#include "tfplus/kv_variable/kernels/byte_swap.h"

namespace tfplus {

// Versioning of the tensor bundle format.
const int kTensorBundleMinProducer = 0;
const int kTensorBundleMinConsumer = 0;
const int kTensorBundleVersion = 1;

// Size of our input buffer for streaming reads
static const int kBufferSize = 1024 * 1024;

// Key to the special BundleHeaderProto entry.  Do not change this, as clients
// can make the assumption that the header is always the first entry in the
// bundle.
const char* const kHeaderEntryKey = "";

namespace {

// Reads "num_elements" string elements from file[offset, offset+size) into the
// length-N "destination".  Discards the original content of "destination".
//
// Checksums the string lengths (as restored uint32 or uint64, not varint64
// bytes) and string bytes, and stores it into "actual_crc32c".
Status ReadStringTensor(io::InputBuffer* buffered_file, size_t num_elements,
                        size_t offset, size_t size, tstring* destination,
                        uint32* actual_crc32c, bool need_to_swap_bytes) {
  if (size == 0) return Status::OK();
  CHECK_GT(size, 0);

  // Reads "num_elements" varint64's from "buffered_file".
  TF_RETURN_IF_ERROR(buffered_file->Seek(offset));
  std::vector<uint64> string_lengths(num_elements);
  for (size_t i = 0; i < num_elements; ++i) {
    TF_RETURN_IF_ERROR(buffered_file->ReadVarint64(&string_lengths[i]));
    if (string_lengths[i] <= UINT32_MAX) {
      // We need to do this because older checkpoints only used uint32s and we
      // should still support them.
      uint32 elem_size_uint32 = static_cast<uint32>(string_lengths[i]);
      if (need_to_swap_bytes) {
        // Checksum would have been computed on the source machine's byte order
        elem_size_uint32 = BYTE_SWAP_32(elem_size_uint32);
      }
      *actual_crc32c = crc32c::Extend(
          *actual_crc32c, reinterpret_cast<const char*>(&elem_size_uint32),
          sizeof(uint32));
    } else {
      uint64 length = string_lengths[i];
      if (need_to_swap_bytes) {
        length = BYTE_SWAP_64(length);
      }
      *actual_crc32c =
          crc32c::Extend(*actual_crc32c, reinterpret_cast<const char*>(&length),
                         sizeof(uint64));
    }
  }
  if (offset + size < buffered_file->Tell()) {
    return errors::DataLoss("String lengths longer than expected offset ",
                            offset + size);
  }

  // Reads the length-checksum.
  uint32 raw_length_checksum = 0;  // Bytes in file
  uint32 length_checksum = 0;      // In-memory representation
  size_t unused_bytes_read = 0;
  TF_RETURN_IF_ERROR(buffered_file->ReadNBytes(
      sizeof(uint32), reinterpret_cast<char*>(&raw_length_checksum),
      &unused_bytes_read));
  length_checksum = need_to_swap_bytes ? BYTE_SWAP_32(raw_length_checksum)
                                       : raw_length_checksum;
  if (crc32c::Unmask(length_checksum) != *actual_crc32c) {
    return errors::DataLoss(
        "The length checksum does not match: expected ",
        strings::Printf("%08u", crc32c::Unmask(length_checksum)),
        " but actual is ", strings::Printf("%08u", *actual_crc32c));
  }
  *actual_crc32c = crc32c::Extend(*actual_crc32c,
                                  reinterpret_cast<char*>(&raw_length_checksum),
                                  sizeof(uint32));

  // Reads the actual string bytes.
  for (size_t i = 0; i < num_elements; ++i) {
    const uint64 string_length = string_lengths[i];
    tstring* buffer = &destination[i];

    buffer->resize(string_length);
    size_t bytes_read = 0;
    TF_RETURN_IF_ERROR(
        buffered_file->ReadNBytes(string_length, &(*buffer)[0], &bytes_read));
    *actual_crc32c = crc32c::Extend(*actual_crc32c, buffer->data(), bytes_read);
  }
  return Status::OK();
}

Status ReadVariantTensor(io::InputBuffer* buffered_file, Tensor* ret,
                         size_t offset, size_t size, uint32* actual_crc32c) {
  // On-disk format:
  //   [varint64 len1][bytes variant1][4 byte checksum]
  //   ..
  //   [varint64 lenN][bytes variantN][4 byte checksum]
  // Var "crc32c" checksums all the lens, variant bytes, individual variant
  // checksums (as uint32, not varint32 bytes).
  if (size == 0) return Status::OK();
  size_t num_elements = ret->NumElements();

  // Reads the actual string bytes.
  TF_RETURN_IF_ERROR(buffered_file->Seek(offset));
  for (size_t i = 0; i < num_elements; ++i) {
    // Read the serialized variant length.
    uint64 string_length = 0;
    TF_RETURN_IF_ERROR(buffered_file->ReadVarint64(&string_length));
    *actual_crc32c = crc32c::Extend(
        *actual_crc32c, reinterpret_cast<const char*>(&string_length),
        sizeof(uint64));
    // Read the actual serialized variant.
    string buffer;
    buffer.resize(string_length);
    size_t bytes_read = 0;
    TF_RETURN_IF_ERROR(
        buffered_file->ReadNBytes(string_length, &buffer[0], &bytes_read));
    *actual_crc32c = crc32c::Extend(*actual_crc32c, buffer.data(), bytes_read);
    VariantTensorDataProto proto;
    if (!proto.ParseFromString(buffer)) {
      return errors::DataLoss("Unable to parse VariantTensorDataProto from ",
                              "buffer of size ", string_length, ". ",
                              "Bundle entry offset: ", offset, " size: ", size);
    }
    Variant v = proto;
    if (!DecodeUnaryVariant(&v)) {
      return errors::Internal("Could not decode variant with type_name: \"",
                              v.TypeName(), "\".  Perhaps you forgot to ",
                              "register a decoder via ",
                              "REGISTER_UNARY_VARIANT_DECODE_FUNCTION?");
    }

    // Read the checksum.
    uint32 checksum = 0;
    size_t unused_bytes_read = 0;
    TF_RETURN_IF_ERROR(buffered_file->ReadNBytes(
        sizeof(uint32), reinterpret_cast<char*>(&checksum),
        &unused_bytes_read));
    if (crc32c::Unmask(checksum) != *actual_crc32c) {
      return errors::DataLoss(
          "The checksum after Variant ", i, " does not match.",
          " Expected: ", strings::Printf("%08u", crc32c::Unmask(checksum)),
          " Actual: ", strings::Printf("%08u", *actual_crc32c));
    }
    *actual_crc32c = crc32c::Extend(
        *actual_crc32c, reinterpret_cast<char*>(&checksum), sizeof(uint32));

    ret->flat<Variant>()(i) = std::move(v);
  }

  return Status::OK();
}

char* GetBackingBuffer(const Tensor& val) {
  CHECK(DataTypeCanUseMemcpy(val.dtype())) << val.dtype();
  return const_cast<char*>(val.tensor_data().data());
}

tstring* GetStringBackingBuffer(const Tensor& val) {
  CHECK_EQ(DT_STRING, val.dtype());
  return const_cast<tstring*>(val.flat<tstring>().data());
}

Status ParseEntryProto(StringPiece key, StringPiece value,
                       protobuf::MessageLite* out) {
  if (!out->ParseFromArray(value.data(), value.size())) {
    return errors::DataLoss("Entry for key ", key, " not parseable.");
  }
  return Status::OK();
}

// Serializes the data bytes of the non-string tensor "val".  Discards the
// original content of "bytes_written", and on OK updates it with number of
// bytes written.
// REQUIRES: val.dtype() != DT_STRING
Status WriteTensor(const Tensor& val, FileOutputBuffer* out,
                   size_t* bytes_written) {
  DCHECK_NE(val.dtype(), DT_STRING);
  DCHECK_NE(val.dtype(), DT_VARIANT);
  *bytes_written = val.TotalBytes();
  char* buf = GetBackingBuffer(val);
  VLOG(1) << "Appending " << *bytes_written << " bytes to file";
  return out->Append(StringPiece(buf, *bytes_written));
}

// Serializes string tensor "val".  "bytes_written" is treated in the same
// fashion as WriteTensor().
//
// Checksums all bytes written and stores it into "crc32c".
// REQUIRES: val.dtype() == DT_STRING
Status WriteStringTensor(const Tensor& val, FileOutputBuffer* out,
                         size_t* bytes_written, uint32* crc32c) {
  // On-disk format:
  //   [varint64 len0]..[varint64 lenL][4 byte cksum on lengths][string bytes]
  // Var "crc32c" checksums the string lengths (as uint64, not varint64 bytes),
  // the length-checksum, and all the string bytes.
  DCHECK_EQ(val.dtype(), DT_STRING);
  const tstring* strings = GetStringBackingBuffer(val);

  // Writes the varint lengths.
  string lengths;
  lengths.reserve(val.NumElements());  // At least 1 byte per element.
  *crc32c = 0;
  for (int64 i = 0; i < val.NumElements(); ++i) {
    const tstring* elem = &strings[i];
    DCHECK_EQ(elem->size(), static_cast<uint64>(elem->size()));
    const uint64 elem_size = static_cast<uint64>(elem->size());

    core::PutVarint64(&lengths, elem_size);
    if (elem_size <= UINT32_MAX) {
      // We need to do this because older checkpoints only used uint32s and we
      // should still support them.
      const uint32 elem_size_uint32 = static_cast<uint32>(elem_size);
      *crc32c = crc32c::Extend(*crc32c,
                               reinterpret_cast<const char*>(&elem_size_uint32),
                               sizeof(uint32));
    } else {
      *crc32c = crc32c::Extend(
          *crc32c, reinterpret_cast<const char*>(&elem_size), sizeof(uint64));
    }
  }
  TF_RETURN_IF_ERROR(out->Append(lengths));
  *bytes_written = lengths.size();

  // Writes the length checksum.
  const uint32 length_checksum = crc32c::Mask(*crc32c);
  TF_RETURN_IF_ERROR(out->Append(StringPiece(
      reinterpret_cast<const char*>(&length_checksum), sizeof(uint32))));
  *crc32c = crc32c::Extend(
      *crc32c, reinterpret_cast<const char*>(&length_checksum), sizeof(uint32));
  *bytes_written += sizeof(uint32);

  // Writes all the string bytes out.
  for (int64 i = 0; i < val.NumElements(); ++i) {
    const tstring* string = &strings[i];
    TF_RETURN_IF_ERROR(out->Append(*string));
    *bytes_written += string->size();
    *crc32c = crc32c::Extend(*crc32c, string->data(), string->size());
  }
  return Status::OK();
}

Status WriteStringTensor(const std::vector<string*>& strings,
                         FileOutputBuffer* out,
                         size_t* bytes_written,
                         uint32* crc32c) {
  // On-disk format:
  //   [varint64 len0]..[varint64 lenL][4 byte cksum on lengths][string bytes]
  // Var "crc32c" checksums the string lengths (as uint64, not varint64 bytes),
  // the length-checksum, and all the string bytes.
  // Writes the varint lengths.
  string lengths;
  lengths.reserve(strings.size());  // At least 1 byte per element.
  *crc32c = 0;
  for (auto elem = strings.begin(); elem != strings.end(); ++elem) {
    DCHECK_EQ((*elem)->size(), static_cast<uint64>((*elem)->size()));
    const uint64 elem_size = static_cast<uint64>((*elem)->size());

    core::PutVarint64(&lengths, elem_size);
    if (elem_size <= UINT32_MAX) {
      // We need to do this because older checkpoints only used uint32s and we
      // should still support them.
      const uint32 elem_size_uint32 = static_cast<uint32>(elem_size);
      *crc32c = crc32c::Extend(*crc32c,
                               reinterpret_cast<const char*>(&elem_size_uint32),
                               sizeof(uint32));
    } else {
      *crc32c = crc32c::Extend(
          *crc32c, reinterpret_cast<const char*>(&elem_size), sizeof(uint64));
    }
  }
  TF_RETURN_IF_ERROR(out->Append(lengths));
  *bytes_written = lengths.size();

  // Writes the length checksum.
  const uint32 length_checksum = crc32c::Mask(*crc32c);
  TF_RETURN_IF_ERROR(out->Append(StringPiece(
      reinterpret_cast<const char*>(&length_checksum), sizeof(uint32))));
  *crc32c = crc32c::Extend(
      *crc32c, reinterpret_cast<const char*>(&length_checksum), sizeof(uint32));
  *bytes_written += sizeof(uint32);

  // Writes all the string bytes out.
  for (auto string = strings.begin(); string != strings.end(); ++string) {
    TF_RETURN_IF_ERROR(out->Append(**string));
    *bytes_written += (*string)->size();
    *crc32c = crc32c::Extend(*crc32c, (*string)->data(), (*string)->size());
  }
  return Status::OK();
}

Status WriteVariantTensor(const Tensor& val, FileOutputBuffer* out,
                          size_t* bytes_written, uint32* crc32c) {
  // On-disk format:
  //   [varint64 len1][bytes variant1][4 byte checksum]
  //   ..
  //   [varint64 lenN][bytes variantN][4 byte checksum]
  // Var "crc32c" checksums all the lens, variant bytes, individual variant
  // checksums (as uint32, not varint32 bytes).
  DCHECK_EQ(val.dtype(), DT_VARIANT);

  *crc32c = 0;
  *bytes_written = 0;
  for (int64 i = 0; i < val.NumElements(); ++i) {
    VariantTensorData data;
    val.flat<Variant>()(i).Encode(&data);
    VariantTensorDataProto proto;
    data.ToProto(&proto);
    string elem;
    proto.SerializeToString(&elem);

    // Write the length of the serialized variant.
    DCHECK_EQ(elem.size(), static_cast<uint64>(elem.size()));
    const auto elem_size = static_cast<uint64>(elem.size());
    string len;
    core::PutVarint64(&len, elem_size);
    TF_RETURN_IF_ERROR(out->Append(len));
    *crc32c = crc32c::Extend(*crc32c, reinterpret_cast<const char*>(&elem_size),
                             sizeof(uint64));
    *bytes_written += len.size();

    // Write the serialized variant.
    TF_RETURN_IF_ERROR(out->Append(elem));
    *crc32c = crc32c::Extend(*crc32c, elem.data(), elem.size());
    *bytes_written += elem.size();

    // Write the checksum.
    const uint32 length_checksum = crc32c::Mask(*crc32c);
    TF_RETURN_IF_ERROR(out->Append(StringPiece(
        reinterpret_cast<const char*>(&length_checksum), sizeof(uint32))));
    *crc32c =
        crc32c::Extend(*crc32c, reinterpret_cast<const char*>(&length_checksum),
                       sizeof(uint32));
    *bytes_written += sizeof(uint32);
  }

  return Status::OK();
}

// Returns whether "slice_spec" is a full slice, with respect to the full shape.
//
// This can happen say, when "slice_spec" is
// "TensorSlice(full_tensor_shape.dims())", or when it is "TensorSlice({{0,
// dim(0)}, ..., {0, dim(N)}})" -- a degenerate case we need to guard against.
bool IsFullSlice(const TensorSlice& slice_spec,
                 const TensorShape& full_tensor_shape) {
  if (slice_spec.IsFull()) {
    return true;
  } else {
    TensorShape sliced_shape;
    slice_spec.SliceTensorShape(full_tensor_shape, &sliced_shape).IgnoreError();
    return sliced_shape == full_tensor_shape;
  }
}

Status CorruptFileError(const Status& in_status, const string& filename,
                        const string& detail) {
  if (in_status.ok()) {
    return errors::Internal("Unable to read file (", filename,
                            "). Perhaps the file is corrupt or was produced by "
                            "a newer version of TensorFlow with format changes "
                            "(",
                            detail, ")");
  }
  return Status(
      in_status.code(),
      strings::StrCat("Unable to read file (", filename,
                      "). Perhaps the file is corrupt or was produced by a "
                      "newer version of TensorFlow with format changes (",
                      detail, "): ", in_status.error_message()));
}

table::Options TableBuilderOptions() {
  table::Options o;
  // Compressed tables cannot be read by TensorFlow releases prior to 1.1.
  // To smoothen the transition, compressed writes are disabled for now
  // (version 1.2) with the intention that they will be enabled again at
  // some point (perhaps the 1.3 release?).
  o.compression = table::kNoCompression;
  return o;
}

// Writes zeros to output buffer to align the next write to the requested
// alignment. "size" is the current size of the buffer and is updated to the
// new size.
Status PadAlignment(FileOutputBuffer* out, int alignment, int64* size) {
  int bytes_over = *size % alignment;
  if (bytes_over == 0) {
    return Status::OK();
  }
  int bytes_to_write = alignment - bytes_over;
  Status status = out->Append(string(bytes_to_write, '\0'));
  if (status.ok()) {
    *size += bytes_to_write;
  }
  return status;
}

}  // namespace

BundleWriter::BundleWriter(Env* env, StringPiece prefix, const Options& options)
    : env_(env),
      options_(options),
      prefix_(prefix),
      tmp_metadata_path_(strings::StrCat(MetaFilename(prefix_), ".tempstate",
                                         random::New64())),
      tmp_data_path_(strings::StrCat(DataFilename(prefix_, 0, 1), ".tempstate",
                                     random::New64())),
      out_(nullptr),
      size_(0),
      curr_entry_(nullptr) {
  status_ = env_->CreateDir(string(io::Dirname(prefix_)));
  if (!status_.ok() && !errors::IsAlreadyExists(status_)) {
    return;
  }
  const string filename = DataFilename(prefix_, 0, 1);
  std::unique_ptr<WritableFile> wrapper;
  #ifdef USE_ORIGIN_TF
  status_ = env_->NewWritableFile(tmp_data_path_, &wrapper);
  #else
  status_ = env_->NewTransactionFile(tmp_data_path_, &wrapper);
  #endif
  if (!status_.ok()) return;
  out_ = std::unique_ptr<FileOutputBuffer>(
      new FileOutputBuffer(wrapper.release(), 8 << 20 /* 8MB write buffer */));

  VLOG(1) << "Writing to file " << tmp_data_path_;
}

Status BundleWriter::Add(StringPiece key, const Tensor& val) {
  if (!status_.ok()) return status_;
  CHECK_NE(key, kHeaderEntryKey);
  const string key_string(key);
  if (entries_.find(key_string) != entries_.end()) {
    status_ = errors::InvalidArgument("Adding duplicate key: ", key);
    return status_;
  }

  BundleEntryProto* entry = &entries_[key_string];
  entry->set_dtype(val.dtype());
  val.shape().AsProto(entry->mutable_shape());
  entry->set_shard_id(0);
  entry->set_offset(size_);

  // Updates the data file.
  size_t data_bytes_written = 0;
  uint32 crc32c = 0;
  out_->clear_crc32c();
  if (val.dtype() == DT_STRING) {
    status_ = WriteStringTensor(val, out_.get(), &data_bytes_written, &crc32c);
  } else if (val.dtype() == DT_VARIANT) {
    status_ = WriteVariantTensor(val, out_.get(), &data_bytes_written, &crc32c);
  } else {
    status_ = WriteTensor(val, out_.get(), &data_bytes_written);
    crc32c = out_->crc32c();
  }

  if (status_.ok()) {
    entry->set_size(data_bytes_written);
    entry->set_crc32c(crc32c::Mask(crc32c));
    size_ += data_bytes_written;
    status_ = PadAlignment(out_.get(), options_.data_alignment, &size_);
  }
  return status_;
}

Status BundleReader::GetValueWithIndices(const BundleEntryProto& entry,
                                         Tensor* val,
                                         const std::vector<int64>& indices) {
  Tensor* ret = val;
  const TensorShape stored_shape(TensorShape(entry.shape()));
  if (val->NumElements() == 0) {
    ret = new Tensor(entry.dtype(), stored_shape);
  }
  // Open the data file if it has not been opened.
  io::InputBuffer* buffered_file = data_[entry.shard_id()];
  if (buffered_file == nullptr) {
    std::unique_ptr<RandomAccessFile> file = nullptr;
    TF_RETURN_IF_ERROR(env_->NewRandomAccessFile(
        DataFilename(prefix_, entry.shard_id(), num_shards_), &file));
    buffered_file = new io::InputBuffer(file.release(), kBufferSize);
    // The InputBuffer and RandomAccessFile objects are both released in dtor.
    data_[entry.shard_id()] = buffered_file;
  }
  CHECK(buffered_file != nullptr);

  TF_RETURN_IF_ERROR(buffered_file->Seek(entry.offset()));
  uint32 actual_crc32c = 0;

  char* backing_buffer = const_cast<char*>((ret->tensor_data().data()));
  size_t unused_bytes_read;
  // prepare buffer for read values
  size_t buffer_size = 16 << 20;
  // allocate dump buffer
  std::shared_ptr<char> read_buffer_shared_ptr(
      static_cast<char*>(malloc(sizeof(char) * buffer_size)), free);
  char* read_buffer = read_buffer_shared_ptr.get();
  if (read_buffer == nullptr) {
    return errors::Internal("KvVariable was failed to allocate memory.");
  }
  size_t total_read_size = entry.size();
  size_t embedding_dim = val->NumElements() / val->dim_size(0);
  size_t embedding_value_size = embedding_dim * DataTypeSize(entry.dtype());
  auto entry_offset = entry.offset();

  size_t read_buffer_head = 0;
  size_t read_buffer_tail = 0;

  auto do_read = [this, &read_buffer_head, &read_buffer_tail, &read_buffer,
                  &buffered_file, &entry_offset,
                  &total_read_size, buffer_size,
                  &embedding_value_size](size_t next_offset, size_t next_size) {
    StringPiece sp;
    TF_RETURN_IF_ERROR(buffered_file->file()->Read(
        entry_offset + next_offset, next_size, &sp, read_buffer));
    if (sp.data() != read_buffer) {
      memmove(read_buffer, sp.data(), next_size);
    }
    read_buffer_head = next_offset;
    read_buffer_tail = read_buffer_head + next_size;
    return Status::OK();
  };

  for (size_t i = 0; i < indices.size(); i++) {
    size_t idx_head = indices[i] * embedding_value_size;
    size_t idx_tail = idx_head + embedding_value_size;
    // whether embedding in buffer
    if (!(idx_head >= read_buffer_head && idx_tail <= read_buffer_tail)) {
      size_t next_offset, next_size;
      next_offset = idx_head;
      if (total_read_size - next_offset > buffer_size) {
        next_size = buffer_size;
      } else {
        next_size = total_read_size - next_offset;
      }
      TF_RETURN_IF_ERROR(do_read(next_offset, next_size));
    }
    memmove(backing_buffer + i * embedding_value_size,
            read_buffer + idx_head - read_buffer_head, embedding_value_size);
  }
  *val = *ret;
  if (ret != val) delete ret;
  return Status::OK();
}


Status BundleReader::LookupWithIndices(StringPiece key, Tensor* val,
                                       const std::vector<int64>& indices) {
  CHECK(val != nullptr);
  BundleEntryProto entry;
  TF_RETURN_IF_ERROR(GetBundleEntryProto(key, &entry));
  if (entry.slices().empty()) {
    return GetValueWithIndices(entry, val, indices);
  } else {
    return errors::Unimplemented("Can not get slice value with indeces");
  }
}



Status BundleWriter::AddSlice(StringPiece full_tensor_key,
                              const TensorShape& full_tensor_shape,
                              const TensorSlice& slice_spec,
                              const Tensor& slice_tensor) {
  if (!status_.ok()) return status_;
  CHECK_NE(full_tensor_key, kHeaderEntryKey);

  // If just a singleton full slice, use the regular Add() to be more efficient.
  if (IsFullSlice(slice_spec, full_tensor_shape)) {
    return Add(full_tensor_key, slice_tensor);
  }

  // Inserts/updates the full tensor's metadata entry.
  //
  // In the case of a sharded save, MergeBundles() is responsible for merging
  // the "slices" field of multiple metadata entries corresponding to the same
  // full tensor.
  const string full_tensor_key_string(full_tensor_key);
  BundleEntryProto* full_entry = &entries_[full_tensor_key_string];
  if (full_entry->dtype() != DT_INVALID) {
    CHECK_EQ(full_entry->dtype(), slice_tensor.dtype());
  }
  if (full_entry->has_shape()) {
    CHECK(TensorShape(full_entry->shape()) == full_tensor_shape);
  }

  // Populates dtype, shape, and slices.  Intentionally leaving out shard_id and
  // offset, which do not make sense for this full tensor entry.
  full_entry->set_dtype(slice_tensor.dtype());
  full_tensor_shape.AsProto(full_entry->mutable_shape());
  TensorSliceProto* slice_proto = full_entry->add_slices();
  slice_spec.AsProto(slice_proto);

  // The slice itself is handled by a regular Add(), which includes adding its
  // own metadata entry, and writing out the slice's values.
  const string slice_name =
      checkpoint::EncodeTensorNameSlice(full_tensor_key_string, slice_spec);
  status_ = Add(slice_name, slice_tensor);
  return status_;
}

Status BundleWriter::AddTensorHeader(StringPiece key, DataType dtype) {
  if (!status_.ok()) return status_;
  CHECK_NE(key, kHeaderEntryKey);
  const string key_string(key);
  if (entries_.find(key_string) != entries_.end()) {
    status_ = errors::InvalidArgument("Adding duplicate key: ", key);
    return status_;
  }

  entry_seg_ = &entries_[key_string];
  entry_seg_->set_dtype(dtype);
  entry_seg_->set_shard_id(0);
  entry_seg_->set_offset(size_);

  out_->clear_crc32c();
  return status_;
}

#ifndef USE_ORIGIN_TF
Status BundleWriter::AddSliceHeader(string tensor_name,
                                    const TensorShape& shape, DataType type,
                                    bool is_hash, TensorSliceProto** proto) {
  if (!status_.ok()) return status_;
  BundleEntryProto* full_entry = &entries_[tensor_name];
  if (full_entry->dtype() != DT_INVALID) {
    CHECK_EQ(full_entry->dtype(), type);
  }
  if (full_entry->has_shape()) {
    CHECK(TensorShape(full_entry->shape()) == shape);
  }

  full_entry->set_is_hash_table(is_hash);
  full_entry->set_dtype(type);
  shape.AsProto(full_entry->mutable_shape());
  *proto = full_entry->add_slices();
  return Status::OK();
}
#endif


Status BundleWriter::AddTensorHeader(StringPiece key, DataType dtype,
                                     TensorShape shape) {
  if (!status_.ok()) return status_;
  CHECK_NE(key, kHeaderEntryKey);
  const string key_string(key);
  if (entries_.find(key_string) != entries_.end()) {
    status_ = errors::InvalidArgument("Adding duplicate key: ", key);
    return status_;
  }

  entry_seg_ = &entries_[key_string];
  entry_seg_->set_dtype(dtype);
  shape.AsProto(entry_seg_->mutable_shape());
  entry_seg_->set_shard_id(0);
  entry_seg_->set_offset(size_);

  out_->clear_crc32c();
  return status_;
}

// use if tensor is less or equal than buffer_size, just dump once
Status BundleWriter::AddCompeleteData(char* content, int64 data_bytes_written) {
  uint32 crc32c = 0;

  status_ = out_->Append(StringPiece(content, data_bytes_written));
  if (!status_.ok()) return status_;

  crc32c = out_->crc32c();

  if (status_.ok()) {
    entry_seg_->set_size(data_bytes_written);
    entry_seg_->set_crc32c(crc32c::Mask(crc32c));
    size_ += data_bytes_written;
  }
  return status_;
}

void BundleWriter::FillTensorShape(TensorShape shape) {
  shape.AsProto(entry_seg_->mutable_shape());
}
// dump mutiple times;
Status BundleWriter::AppendSegmentData(char* content,
                                       int64 data_bytes_written) {
  return out_->AppendSegment(StringPiece(content, data_bytes_written));
}

void BundleWriter::EndSegmentData(int64 total_bytes_written,
                                  int64 end_bytes_written) {
  // out_->EndSegment(end_bytes_written);
  uint32 crc32c = out_->crc32c();

  entry_seg_->set_size(total_bytes_written);
  entry_seg_->set_crc32c(crc32c::Mask(crc32c));
  size_ += total_bytes_written;
}

// TODO(zongheng): on metadata write failure or !status_.ok(), consider removing
// the orphaned data file.
Status BundleWriter::Finish() {
  if (out_) {
    status_.Update(out_->Close());
    out_ = nullptr;
    if (status_.ok()) {
      #ifdef USE_ORIGIN_TF
      status_ = Env::Default()->RenameFile(
          tmp_data_path_, DataFilename(prefix_, 0, 1));
      #else
      status_ = Env::Default()->TransactionRenameFile(
          tmp_data_path_, DataFilename(prefix_, 0, 1));
      #endif
    } else {
      Env::Default()->DeleteFile(tmp_data_path_).IgnoreError();
    }
  }
  if (!status_.ok()) return status_;
  // Build key -> BundleEntryProto table.
  std::unique_ptr<WritableFile> file;
  #ifdef USE_ORIGIN_TF
  status_ = env_->NewWritableFile(tmp_metadata_path_, &file);
  #else
  status_ = env_->NewTransactionFile(tmp_metadata_path_, &file);
  #endif
  if (!status_.ok()) return status_;
  {
    // N.B.: the default use of Snappy compression may not be supported on all
    // platforms (e.g. Android).  The metadata file is small, so this is fine.
    table::Options options;
    options.compression = table::kNoCompression;
    table::TableBuilder builder(options, file.get());
    // Header entry.
    BundleHeaderProto header;
    header.set_num_shards(1);
    header.set_endianness(BundleHeaderProto::LITTLE);
    if (!port::kLittleEndian) header.set_endianness(BundleHeaderProto::BIG);
    VersionDef* version = header.mutable_version();
    version->set_producer(kTensorBundleVersion);
    version->set_min_consumer(kTensorBundleMinConsumer);

    builder.Add(kHeaderEntryKey, header.SerializeAsString());

    // All others.
    for (const auto& p : entries_) {
      builder.Add(p.first, p.second.SerializeAsString());
    }
    status_ = builder.Finish();
  }
  status_.Update(file->Close());
  if (!status_.ok()) {
    Env::Default()->DeleteFile(tmp_metadata_path_).IgnoreError();
    return status_;
  } else {
  #ifdef USE_ORIGIN_TF
  status_ = Env::Default()->RenameFile(tmp_metadata_path_,
                                       MetaFilename(prefix_));
  #else
  status_ = Env::Default()->TransactionRenameFile(
               tmp_metadata_path_, MetaFilename(prefix_));
    #endif
    if (!status_.ok()) return status_;
  }
  status_ = errors::Internal("BundleWriter is closed");
  return Status::OK();
}

// tfplus special interface

// Begin to write the chunks of data
Status BundleWriter::BeginWriteChunkData(StringPiece key,
                                         DataType dtype,
                                         TensorShape shape) {
  if (!status_.ok()) return status_;
  CHECK_NE(key, kHeaderEntryKey);
  const string key_string(key);
  if (entries_.find(key_string) != entries_.end()) {
    status_ = errors::InvalidArgument("Adding duplicate key: ", key);
    return status_;
  }
  if (curr_entry_ != nullptr) {
    status_ = errors::Internal("Call `BundleWriter::EndWriteChunkData` "
                               "method after write.");
    return status_;
  }
  // assign current entry
  curr_entry_ = &entries_[key_string];
  curr_entry_->set_dtype(dtype);
  shape.AsProto(curr_entry_->mutable_shape());
  curr_entry_->set_shard_id(0);
  curr_entry_->set_offset(size_);
  // reset crc32
  out_->clear_crc32c();
  return status_;
}

// The bytes to be writed less than file buffer size
Status BundleWriter::WriteOneBufferData(
    char* content, int64 data_bytes_written) {
  // reset crc32
  uint32 crc32c = 0;
  status_ = out_->Append(StringPiece(content, data_bytes_written));
  if (!status_.ok()) return status_;
  // get crc32 from FileOutputBuffer
  crc32c = out_->crc32c();
  // update the crc32 of current entry
  if (status_.ok()) {
    curr_entry_->set_size(data_bytes_written);
    curr_entry_->set_crc32c(crc32c::Mask(crc32c));
    size_ += data_bytes_written;
  }
  curr_entry_ = nullptr;
  return status_;
}

// Append one chunk data to FileOutputBuffer
Status BundleWriter::WriteChunkData(char* content, int64 data_bytes_written) {
  return out_->AppendChunk(StringPiece(content, data_bytes_written));
}

// End to write the chunks of data
void BundleWriter::EndWriteChunkData(int64 total_bytes_written,
                                     int64 end_bytes_written) {
  uint32 crc32c = out_->crc32c();
  curr_entry_->set_size(total_bytes_written);
  curr_entry_->set_crc32c(crc32c::Mask(crc32c));
  size_ += total_bytes_written;
  curr_entry_ = nullptr;
}

// Merging tensor bundles.

// Accumulator of metadata states during a merge.
struct MergeState {
  // Accumulated from the header entries.
  int num_shards = 0;

  // Derives "endianness" and "version" from the first bundle merged (hence the
  // "seen_first_bundle" guard).  The two fields must be the same for all
  // bundles in a merge.
  bool seen_first_bundle = false;
  BundleHeaderProto_Endianness endianness;
  VersionDef version;

  // Tensor key -> BundleEntryProto.
  std::map<string, BundleEntryProto> entries;
  // Data file path -> new shard id in the final merged bundle.
  std::unordered_map<string, int32> shard_ids;
};

// Merges entries of "prefix" into the accumulator state "merge".
// Returns OK iff the merge succeeds.
static Status MergeOneBundle(Env* env, StringPiece prefix,
                             MergeState* merge_state) {
  VLOG(1) << "Merging bundle:" << prefix;
  const string filename = MetaFilename(prefix);
  uint64 file_size;
  TF_RETURN_IF_ERROR(env->GetFileSize(filename, &file_size));
  std::unique_ptr<RandomAccessFile> file;
  TF_RETURN_IF_ERROR(env->NewRandomAccessFile(filename, &file));

  table::Table* table = nullptr;
  TF_RETURN_IF_ERROR(
      table::Table::Open(TableBuilderOptions(), file.get(), file_size, &table));
  std::unique_ptr<table::Table> table_deleter(table);
  std::unique_ptr<table::Iterator> iter(table->NewIterator());

  int num_shards;
  // Process header.
  {
    iter->Seek(kHeaderEntryKey);
    if (!iter->Valid()) {
      return CorruptFileError(iter->status(), filename,
                              "failed to seek to header entry");
    }
    BundleHeaderProto header;
    Status s = ParseEntryProto(iter->key(), iter->value(), &header);
    if (!s.ok()) return CorruptFileError(s, filename, "unable to parse header");

    merge_state->num_shards += header.num_shards();
    if (!merge_state->seen_first_bundle) {
      merge_state->seen_first_bundle = true;
      merge_state->endianness = header.endianness();
      merge_state->version = header.version();
    } else {
      // Validates "endianness".
      if (merge_state->endianness != header.endianness()) {
        return errors::InvalidArgument(
            "Merging bundles with conflicting endianness; inputs corrupted?");
      }
      // Validates "version".
      string curr_version, merge_version;
      header.version().SerializeToString(&curr_version);
      merge_state->version.SerializeToString(&merge_version);
      if (curr_version != merge_version) {
        return errors::InvalidArgument(
            "Merging bundles with different format versions: merged ",
            merge_version, " vs. curr ", curr_version);
      }
    }
    num_shards = header.num_shards();
    iter->Next();
  }

  // Loops through the non-header to-merge entries.
  BundleEntryProto to_merge_entry;
  for (; iter->Valid(); iter->Next()) {
    const string key(iter->key());
    const auto entry_iter = merge_state->entries.find(key);

    // Illegal: the duplicated entry is a non-slice tensor.
    if (entry_iter != merge_state->entries.end() &&
        entry_iter->second.slices().empty()) {
      return errors::InvalidArgument(
          "Duplicate tensor keyed by ", key,
          " encountered, when merging prefix: ", prefix);
    }

    TF_RETURN_IF_ERROR(
        ParseEntryProto(iter->key(), iter->value(), &to_merge_entry));

    // The duplicated entry holds metadata for a sliced full tensor.
    // Allows the duplication and merges "slices".
    if (entry_iter != merge_state->entries.end()) {
      BundleEntryProto& existing_entry = entry_iter->second;
      if (to_merge_entry.slices().empty()) {
        return errors::Internal(
            "Duplicate tensor keyed by ", key,
            "; attempting to merge in a non-slice bundle entry");
      }
      // Only needs merge the "slices" field (and validate dtype/shape).
      for (int i = 0; i < to_merge_entry.slices_size(); ++i) {
        TensorSliceProto* slot = existing_entry.add_slices();
        *slot = to_merge_entry.slices(i);
      }
      CHECK_EQ(existing_entry.dtype(), to_merge_entry.dtype());
      CHECK(TensorShape(existing_entry.shape()) ==
            TensorShape(to_merge_entry.shape()));
      continue;
    }

    // Key doesn't duplicate: a fresh tensor/slice entry.
    auto result = merge_state->shard_ids.insert(
        {DataFilename(prefix, to_merge_entry.shard_id(), num_shards),
         merge_state->shard_ids.size()});
    to_merge_entry.set_shard_id(result.first->second);
    merge_state->entries[key] = to_merge_entry;
  }
  return Status::OK();
}

#ifndef USE_ORIGIN_TF
Status FixMergeHashTableBundles(MergeState* state) {
  std::unordered_map<string, string> bundle_mapping;
  for (auto&& item : state->entries) {
    if (!item.second.is_hash_table()) {
      continue;
    }
    std::multimap<int64, TensorSliceProto*> sorter;
    for (int slice = 0; slice < item.second.slices_size(); slice++) {
      sorter.emplace(item.second.slices(slice).hash_slice_begin(),
                     item.second.mutable_slices(slice));
    }
    int64 idx = 0;
    std::vector<TensorSliceProto> slices;
    for (auto&& itemx : sorter) {
      if (itemx.second->extent(0).length() > 0) {
        slices.emplace_back();
        TensorSliceProto& slice = slices.back();
        slice.CopyFrom(*itemx.second);
        slice.mutable_extent(0)->set_start(idx);
        idx += slice.extent(0).length();
        TensorSlice from_slice(1);
        from_slice.set_start(0, slice.hash_slice_begin());
        from_slice.set_length(0, slice.hash_slice_length());
        string from = checkpoint::EncodeTensorNameSlice(item.first, from_slice);
        string to =
            checkpoint::EncodeTensorNameSlice(item.first, TensorSlice(slice));
        if (!bundle_mapping.emplace(from, to).second) {
          return errors::FailedPrecondition(
              "FixMergeHashTableBundles has some error when create bundle "
              "mapping.");
        }
      } else {
        TensorSlice from_slice(1);
        from_slice.set_start(0, itemx.second->hash_slice_begin());
        from_slice.set_length(0, itemx.second->hash_slice_length());
        string from = checkpoint::EncodeTensorNameSlice(item.first, from_slice);
        if (!bundle_mapping.emplace(from, "").second) {
          return errors::FailedPrecondition(
              "FixMergeHashTableBundles has some error when create bundle "
              "mapping. 2");
        }
      }
    }
    item.second.clear_slices();
    for (auto&& itemx : slices) {
      item.second.add_slices()->CopyFrom(itemx);
    }
    item.second.mutable_shape()->mutable_dim(0)->set_size(idx);
  }
  std::map<string, BundleEntryProto> entries_tmp;
  entries_tmp.swap(state->entries);
  for (auto&& item : entries_tmp) {
    auto iter = bundle_mapping.find(item.first);
    string real_name;
    if (iter == bundle_mapping.end()) {
      real_name = item.first;
    } else {
      real_name = iter->second;
    }
    if (real_name == "") {
      LOG(INFO) << "Ignore Hash Table: " << str_util::CEscape(item.first);
      continue;
    }
    state->entries.emplace(real_name, item.second);
  }
  return Status::OK();
}
#endif

Status RenameBundlesInParallel(Env* env, thread::ThreadPool* pool,
                               const MergeState* merge,
                               StringPiece merged_prefix) {
  const uint32 shard_size = merge->shard_ids.size();
  // running/finished count of scheduled works
  std::atomic<uint32> finished_works(0);
  // overall status scheduled works
  mutex status_mutex;
  Status overall_status GUARDED_BY(status_mutex);
  // Renames data files to contain the merged bundle prefix.
  for (const auto& p : merge->shard_ids) {
    pool->Schedule([=, &status_mutex, &overall_status, &finished_works]() {
      VLOG(1) << "Renaming " << p.first << " to "
              << DataFilename(merged_prefix, p.second, shard_size);
      #ifdef USE_ORIGIN_TF
      Status status = env->RenameFile(
          p.first, DataFilename(merged_prefix, p.second, shard_size));
      #else
      Status status = env->TransactionRenameFile(
          p.first, DataFilename(merged_prefix, p.second, shard_size));
      #endif
      {
        mutex_lock l(status_mutex);
        overall_status.Update(status);
      }
      finished_works++;
    });
  }
  // Waits until all scheduled work has finished.
  while (finished_works < shard_size) {
    std::this_thread::yield();
  }
  return overall_status;
}

Status MergeBundles(Env* env, gtl::ArraySlice<tstring> prefixes,
                    StringPiece merged_prefix, thread::ThreadPool* pool) {
  // Merges all metadata tables.
  // TODO(zhifengc): KeyValue sorter if it becomes too big.
  MergeState merge;
  Status status = env->CreateDir(string(io::Dirname(merged_prefix)));
  if (!status.ok() && !errors::IsAlreadyExists(status)) return status;
  for (int i = 0; i < prefixes.size(); ++i) {
    TF_RETURN_IF_ERROR(MergeOneBundle(env, prefixes[i], &merge));
  }
  #ifndef USE_ORIGIN_TF
  TF_RETURN_IF_ERROR(FixMergeHashTableBundles(&merge));
  #endif
  if (pool == nullptr) {
    // Renames data files to contain the merged bundle prefix.
    for (const auto& p : merge.shard_ids) {
      VLOG(1) << "Renaming " << p.first << " to "
              << DataFilename(merged_prefix, p.second, merge.shard_ids.size());
      // TF_RETURN_IF_ERROR(env->RenameFile(
      TF_RETURN_IF_ERROR(
          #ifdef USE_ORIGIN_TF
          env->RenameFile(p.first, DataFilename(merged_prefix,
                          p.second, merge.shard_ids.size())));
          #else
          env->TransactionRenameFile(p.first, DataFilename(merged_prefix,
                          p.second, merge.shard_ids.size())));
          #endif
    }
  } else {
    // RenameFile call can be expensive for some FS like oss. Parallelizing it.
    TF_RETURN_IF_ERROR(
        RenameBundlesInParallel(env, pool, &merge, merged_prefix));
  }

  // Writes the final metadata table under the merged prefix.
  std::unique_ptr<WritableFile> merged_metadata;
  TF_RETURN_IF_ERROR(
     #ifdef USE_ORIGIN_TF
     env->NewWritableFile(MetaFilename(merged_prefix), &merged_metadata));
     #else
     env->NewTransactionFile(MetaFilename(merged_prefix), &merged_metadata));
     #endif
  {
    table::TableBuilder builder(TableBuilderOptions(), merged_metadata.get());
    // Header entry.
    BundleHeaderProto header;
    header.set_num_shards(merge.num_shards);
    header.set_endianness(merge.endianness);
    *header.mutable_version() = merge.version;
    builder.Add(kHeaderEntryKey, header.SerializeAsString());
    // All others.
    for (const auto& p : merge.entries) {
      builder.Add(p.first, p.second.SerializeAsString());
    }
    status = builder.Finish();
  }
  status.Update(merged_metadata->Close());
  if (!status.ok()) return status;
  VLOG(1) << "Merged bundles to:" << merged_prefix;

  // Cleanup: best effort based and ignores errors.
  for (const string& prefix : prefixes) {
    env->DeleteFile(MetaFilename(prefix)).IgnoreError();
  }
  return status;
}

// Interface for reading a tensor bundle.

BundleReader::BundleReader(Env* env, StringPiece prefix)
    : env_(env),
      prefix_(prefix),
      metadata_(nullptr),
      table_(nullptr),
      iter_(nullptr),
      need_to_swap_bytes_(false) {
  const string filename = MetaFilename(prefix_);
  uint64 file_size;
  status_ = env_->GetFileSize(filename, &file_size);
  if (!status_.ok()) return;

  // Opens the metadata table.
  std::unique_ptr<RandomAccessFile> wrapper;
  status_ = env_->NewRandomAccessFile(filename, &wrapper);
  if (!status_.ok()) return;
  metadata_ = wrapper.release();
  status_ = table::Table::Open(table::Options(), metadata_, file_size, &table_);
  if (!status_.ok()) return;
  iter_ = table_->NewIterator();

  // Reads "num_shards_" from the first entry.
  iter_->Seek(kHeaderEntryKey);
  if (!iter_->Valid()) {
    status_ = CorruptFileError(iter_->status(), filename,
                               "failed to seek to header entry");
    return;
  }
  BundleHeaderProto header;
  status_ = ParseEntryProto(iter_->key(), iter_->value(), &header);
  if (!status_.ok()) {
    status_ = CorruptFileError(status_, filename, "unable to parse header");
    return;
  }
  num_shards_ = header.num_shards();
  if ((header.endianness() == BundleHeaderProto::BIG && port::kLittleEndian) ||
      (header.endianness() == BundleHeaderProto::LITTLE &&
       !port::kLittleEndian)) {
    need_to_swap_bytes_ = true;
  }
  status_ = CheckVersions(header.version(), kTensorBundleVersion,
                          kTensorBundleMinProducer, "Checkpoint", "checkpoint");
}

int BundleReader::CalcNumShardsByTensorName(const string& prefix_name,
                                            const string& suffix_name) {
  int left = 0;
  int right = 1023;
  while (left < right) {
    int mid = (left + right + 1) / 2;
    if (PartExists(mid, prefix_name, suffix_name)) {
      left = mid;
    } else {
      right = mid - 1;
    }
  }
  if (PartExists(left, prefix_name, suffix_name)) {
    return left + 1;
  } else {
    // Variable regardless of part
    return 0;
  }
}
bool BundleReader::PartExists(int part_id, const string& prefix_name,
                              const string& suffix_name) {
  std::string part_name = "/part_" + std::to_string(part_id);
  std::string key = prefix_name + part_name + suffix_name;
  return Contains(key);
}
// bool BundleReader::Contains(StringPiece key) {
//   Seek(key);
//   return Valid() && (this->key() == key);
// }

BundleReader::~BundleReader() {
  delete metadata_;
  delete iter_;
  delete table_;
  // InputBuffer does not own the underlying RandomAccessFile.
  for (auto pair : data_) {
    if (pair.second != nullptr && pair.second->file() != nullptr) {
      delete pair.second->file();
    }
  }
  gtl::STLDeleteValues(&data_);
  gtl::STLDeleteValues(&tensor_slices_);
}

Status BundleReader::GetBundleEntryProto(StringPiece key,
                                         BundleEntryProto* entry) {
  entry->Clear();
  TF_CHECK_OK(status_);
  Seek(key);
  if (!iter_->Valid() || iter_->key() != key) {
    return errors::NotFound("Key ", key, " not found in checkpoint");
  }

  BundleEntryProto entry_copy;
  TF_RETURN_IF_ERROR(
      ParseEntryProto(iter_->key(), iter_->value(), &entry_copy));
  if (!TensorShape::IsValid(entry_copy.shape())) {
    return errors::DataLoss("Invalid tensor shape: ", key, " ",
                            ProtoShortDebugString(entry_copy.shape()));
  }

  *entry = entry_copy;
  return Status::OK();
}

Status BundleReader::GetValue(const BundleEntryProto& entry, Tensor* val) {
  Tensor* ret = val;
  const TensorShape stored_shape(TensorShape(entry.shape()));
  if (val->NumElements() == 0) {
    ret = new Tensor(entry.dtype(), stored_shape);
  }

  // Validates the "size" field.
  if (entry.dtype() != DT_STRING && entry.dtype() != DT_VARIANT) {
    if (entry.size() != ret->TotalBytes()) {
      return errors::DataLoss("Invalid size in bundle entry: key ", key(),
                              "; stored size ", entry.size(),
                              "; expected size ", ret->TotalBytes());
    }
  } else if (entry.dtype() == DT_STRING) {
    // Relaxes the check for string tensors as follows:
    //   entry.size() == bytes(varint lengths) + bytes(data)
    //                >= NumElems + bytes(data), since size bytes(varint) >= 1.
    //   TotalBytes() == sizeof(tstring) * NumElems + bytes(data)
    // Since we don't know bytes(varint lengths), we just check an inequality.
    const size_t lower_bound = ret->NumElements() + ret->TotalBytes() -
                               sizeof(tstring) * ret->NumElements();
    if (entry.size() < lower_bound) {
      return errors::DataLoss("Invalid size in bundle entry: key ", key(),
                              "; stored size ", entry.size(),
                              "; expected size is at least ", lower_bound);
    }
  }

  // Open the data file if it has not been opened.
  io::InputBuffer* buffered_file = data_[entry.shard_id()];
  if (buffered_file == nullptr) {
    std::unique_ptr<RandomAccessFile> file = nullptr;
    TF_RETURN_IF_ERROR(env_->NewRandomAccessFile(
        DataFilename(prefix_, entry.shard_id(), num_shards_), &file));
    buffered_file = new io::InputBuffer(file.release(), kBufferSize);
    // The InputBuffer and RandomAccessFile objects are both released in dtor.
    data_[entry.shard_id()] = buffered_file;
  }
  CHECK(buffered_file != nullptr);

  TF_RETURN_IF_ERROR(buffered_file->Seek(entry.offset()));
  uint32 actual_crc32c = 0;

  if (DataTypeCanUseMemcpy(entry.dtype())) {
    char* backing_buffer = const_cast<char*>((ret->tensor_data().data()));
    size_t unused_bytes_read;
    if (entry.size() > kBufferSize) {
      StringPiece sp;
      TF_RETURN_IF_ERROR(buffered_file->file()->Read(
          entry.offset(), entry.size(), &sp, backing_buffer));
      if (sp.data() != backing_buffer) {
        memmove(backing_buffer, sp.data(), entry.size());
      }
    } else {
      TF_RETURN_IF_ERROR(buffered_file->ReadNBytes(entry.size(), backing_buffer,
                                                   &unused_bytes_read));
    }
    // Note that we compute the checksum *before* byte-swapping. The checksum
    // should be on the bytes in the order they appear in the file.
    actual_crc32c = crc32c::Value(backing_buffer, entry.size());
    if (need_to_swap_bytes_) {
      TF_RETURN_IF_ERROR(ByteSwapTensor(ret));
    }
  } else if (entry.dtype() == DT_VARIANT) {
    if (need_to_swap_bytes_) {
      return errors::Unimplemented(
          "TensorBundle at ", prefix_,
          "is of a different endianness than this machine's hardware, and "
          "the bundle contains a variant (arbitrary C++ type) tensor. "
          "Byte-swapping of variant tensors is not currently implemented.");
    }
    // Relies on io::InputBuffer's buffering, because we issue many neighboring
    // reads for a single string tensor.
    TF_RETURN_IF_ERROR(ReadVariantTensor(buffered_file, ret, entry.offset(),
                                         entry.size(), &actual_crc32c));
  } else {
    // Relies on io::InputBuffer's buffering, because we issue many neighboring
    // reads for a single string tensor.
    TF_RETURN_IF_ERROR(ReadStringTensor(
        buffered_file, ret->NumElements(), entry.offset(), entry.size(),
        GetStringBackingBuffer(*ret), &actual_crc32c, need_to_swap_bytes_));
  }
  if (crc32c::Unmask(entry.crc32c()) != actual_crc32c) {
    return errors::DataLoss(
        "Checksum does not match: stored ",
        strings::Printf("%08u", crc32c::Unmask(entry.crc32c())),
        " vs. calculated on the restored bytes ", actual_crc32c);
  }

  *val = *ret;
  if (ret != val) delete ret;
  return Status::OK();
}

Status BundleReader::Lookup(StringPiece key, Tensor* val) {
  CHECK(val != nullptr);
  BundleEntryProto entry;
  TF_RETURN_IF_ERROR(GetBundleEntryProto(key, &entry));

  if (entry.slices().empty()) {
    return GetValue(entry, val);
  } else {
    return GetSliceValue(
        key, entry,
        /* a full slice */ TensorSlice(TensorShape(entry.shape()).dims()), val);
  }
}

Status BundleReader::LookupHeader(StringPiece tensor_key, int64 total_bytes) {
  BundleEntryProto entry;
  TF_RETURN_IF_ERROR(GetBundleEntryProto(tensor_key, &entry));
  if (entry.size() != total_bytes) {
    return errors::DataLoss("Invalid size in bundle entry: key ", key(),
                            "; stored size ", entry.size(), "; expected size ",
                            total_bytes);
  }
  io::InputBuffer* buffered_file = data_[entry.shard_id()];
  if (buffered_file == nullptr) {
    std::unique_ptr<RandomAccessFile> file = nullptr;
    TF_RETURN_IF_ERROR(env_->NewRandomAccessFile(
        DataFilename(prefix_, entry.shard_id(), num_shards_), &file));
    buffered_file =
        new io::InputBuffer(file.release(), 256 << 10 /* 256KB buffer */);
    // The InputBuffer and RandomAccessFile objects are both released in dtor.
    data_[entry.shard_id()] = buffered_file;
  }
  CHECK(buffered_file != nullptr);

  TF_RETURN_IF_ERROR(buffered_file->Seek(entry.offset()));
  if (!DataTypeCanUseMemcpy(entry.dtype())) {
    return errors::DataLoss("segment lookup not support string");
  }
  LookupSegItem seg_item;
  seg_item.entry = entry;
  seg_item.total_size = entry.size();
  seg_item.bytes_read = 0;

  tmp_lookupseg_items_[string(tensor_key)] = seg_item;
  return Status::OK();
}

Status BundleReader::LookupSegment(StringPiece key, size_t buffer_size,
                                   char* destination, size_t& real_bytes_read) {
  LookupSegItem& seg_item = tmp_lookupseg_items_[string(key)];
  const size_t desired_bytes = std::min(buffer_size, seg_item.total_size);
  if (desired_bytes == 0) {
    real_bytes_read = 0;
    return Status::OK();
  }

  io::InputBuffer* buffered_file = data_[seg_item.entry.shard_id()];
  StringPiece result;
  Status status =
      buffered_file->file()->Read(seg_item.entry.offset() + seg_item.bytes_read,
                                  desired_bytes, &result, destination);

  if (!status.ok()) {
    return errors::InvalidArgument(
        "Read Error! ", buffer_size, " ", seg_item.total_size, " ",
        seg_item.entry.offset() + seg_item.bytes_read, " ", desired_bytes, " ",
        status.ToString());
  }
  if (result.size() != desired_bytes) {
    return errors::DataLoss("Requested ", desired_bytes, " bytes but read ",
                            result.size(), " bytes.");
  }
  // Data is already in the correct location.
  seg_item.bytes_read += result.size();
  seg_item.total_size -= result.size();
  real_bytes_read = result.size();
  return Status::OK();
}

Status BundleReader::LookupSegmentOffset(StringPiece key, uint64_t offset,
                                         size_t buffer_size, char* destination,
                                         size_t& real_bytes_read) {
  LookupSegItem& seg_item = tmp_lookupseg_items_[string(key)];
  const size_t desired_bytes = std::min(buffer_size, seg_item.total_size);
  if (desired_bytes == 0) {
    real_bytes_read = 0;
    return Status::OK();
  }

  io::InputBuffer* buffered_file = data_[seg_item.entry.shard_id()];
  StringPiece result;
  Status status = buffered_file->file()->Read(
      seg_item.entry.offset() + offset, desired_bytes, &result, destination);

  if (!status.ok()) {
    return errors::InvalidArgument(
        "Read Error! ", buffer_size, " ", seg_item.total_size, " ",
        seg_item.entry.offset() + seg_item.bytes_read, " ", desired_bytes, " ",
        status.ToString());
  }
  if (result.size() != desired_bytes) {
    return errors::DataLoss("Requested ", desired_bytes, " bytes but read ",
                            result.size(), " bytes.");
  }
  // Data is already in the correct location.
  seg_item.bytes_read += result.size();
  seg_item.total_size -= result.size();
  real_bytes_read = result.size();
  return Status::OK();
}

Status FileOutputBuffer::AppendChunk(StringPiece data) {
  TF_RETURN_IF_ERROR(FlushBuffer());
  memcpy(&buffer_[0], data.data(), data.size());
  crc32c_ = crc32c::Extend(crc32c_, &buffer_[0], data.size());
  position_ = data.size();
  TF_RETURN_IF_ERROR(FlushBuffer());
  return Status::OK();
}

Status BundleWriter::AddStringTensor(StringPiece key,
                                     const std::vector<string*>& strings,
                                     TensorShape shape) {
  if (!status_.ok()) return status_;
  CHECK_NE(key, kHeaderEntryKey);
  const string key_string(key);
  if (entries_.find(key_string) != entries_.end()) {
    status_ = errors::InvalidArgument("Adding duplicate key: ", key);
    return status_;
  }

  BundleEntryProto* entry = &entries_[key_string];
  entry->set_dtype(DT_STRING);
  shape.AsProto(entry->mutable_shape());
  entry->set_shard_id(0);
  entry->set_offset(size_);

  // Updates the data file.
  size_t data_bytes_written = 0;
  uint32 crc32c = 0;
  out_->clear_crc32c();
  status_ =
      WriteStringTensor(strings, out_.get(), &data_bytes_written, &crc32c);

  if (status_.ok()) {
    entry->set_size(data_bytes_written);
    entry->set_crc32c(crc32c::Mask(crc32c));
    size_ += data_bytes_written;
    status_ = PadAlignment(out_.get(), options_.data_alignment, &size_);
  }
  return status_;
}

Status BundleReader::GetTensorInfo(StringPiece key, int64* size,
                                   std::unique_ptr<RandomAccessFile>* file,
                                   int64* offset) {
  BundleEntryProto entry;
  TF_RETURN_IF_ERROR(GetBundleEntryProto(key, &entry));
  TF_RETURN_IF_ERROR(env_->NewRandomAccessFile(
      DataFilename(prefix_, entry.shard_id(), num_shards_), file));
  *size = entry.size();
  *offset = entry.offset();
  return Status::OK();
}

Status BundleReader::ReadCurrent(Tensor* val) {
  CHECK(val != nullptr);
  BundleEntryProto entry;
  TF_RETURN_IF_ERROR(ParseEntryProto(iter_->key(), iter_->value(), &entry));
  if (!TensorShape::IsValid(entry.shape())) {
    return errors::DataLoss("Invalid tensor shape: ", iter_->key(), " ",
                            ProtoShortDebugString(entry.shape()));
  }

  if (entry.slices().empty()) {
    return GetValue(entry, val);
  } else {
    return GetSliceValue(
        iter_->key(), entry,
        /* a full slice */ TensorSlice(TensorShape(entry.shape()).dims()), val);
  }
}

Status BundleReader::LookupTensorSlices(StringPiece key,
                                        std::vector<TensorSlice>* slices) {
  slices->clear();
  BundleEntryProto entry;
  TF_RETURN_IF_ERROR(GetBundleEntryProto(key, &entry));
  slices->reserve(entry.slices_size());
  for (const auto& slice : entry.slices()) {
    slices->emplace_back(slice);
  }
  return Status::OK();
}

Status BundleReader::LookupSlice(StringPiece full_tensor_key,
                                 const TensorSlice& slice_spec, Tensor* val) {
  CHECK(val != nullptr);
  BundleEntryProto entry;
  TF_RETURN_IF_ERROR(GetBundleEntryProto(full_tensor_key, &entry));
  return GetSliceValue(full_tensor_key, entry, slice_spec, val);
}

Status BundleReader::LookupTensorSliceProtos(
    StringPiece key, std::vector<TensorSliceProto>* slices) {
  slices->clear();
  BundleEntryProto entry;
  TF_RETURN_IF_ERROR(GetBundleEntryProto(key, &entry));
  slices->reserve(entry.slices_size());
  for (const auto& slice : entry.slices()) {
    slices->emplace_back(slice);
  }
  return Status::OK();
}

Status BundleReader::GetSliceValue(StringPiece full_tensor_key,
                                   const BundleEntryProto& full_tensor_entry,
                                   const TensorSlice& slice_spec, Tensor* val) {
  using checkpoint::RegisterTensorSlice;
  using checkpoint::TensorSliceSet;
  DCHECK_GE(full_tensor_entry.slices_size(), 0);

  const TensorShape full_shape(TensorShape(full_tensor_entry.shape()));
  std::vector<std::pair<TensorSlice, string>> details;
  const string full_tensor_key_string(full_tensor_key);
  const TensorSliceSet* tss =
      gtl::FindPtrOrNull(tensor_slices_, full_tensor_key_string);

  // Populates the "full tensor key -> TensorSliceSet" cache.
  if (tss == nullptr) {
    if (full_tensor_entry.slices().empty()) {
      // Special case: a writer has saved a tensor fully, but the reader wants
      // to read in slices.  We therefore register the full slice on-demand here
      // without further complicating the on-disk bundle format.
      TF_RETURN_IF_ERROR(RegisterTensorSlice(
          full_tensor_key_string, full_shape, full_tensor_entry.dtype(),
          /* tag */ "",
          /* full slice */ TensorSlice(full_shape.dims()), &tensor_slices_));
    }
    for (const TensorSliceProto& slice : full_tensor_entry.slices()) {
      TF_RETURN_IF_ERROR(RegisterTensorSlice(
          full_tensor_key_string, full_shape, full_tensor_entry.dtype(),
          /* tag */ "", TensorSlice(slice), &tensor_slices_));
    }
    tss = gtl::FindPtrOrNull(tensor_slices_, full_tensor_key_string);
    CHECK_NE(tss, nullptr);
  }
  if (!tss->QueryMeta(slice_spec, &details)) {
    return errors::InvalidArgument(
        "Does not have sufficient slices for partitioned tensor ",
        full_tensor_key,
        " to restore in slice_spec: ", slice_spec.DebugString());
  }

  // The union of the slices in "details" covers "slice_spec".  Performs the
  // copies from each.
  BundleEntryProto stored_slice_entry = full_tensor_entry;
  for (const auto& slice_tag_pair : details) {
    // Seeks for the stored slice.
    const TensorSlice& stored_slice = slice_tag_pair.first;

    // We already have the entry for the full tensor, so don't query again if
    // the slice is full.
    if (!stored_slice.IsFull()) {
      const string encoded_stored_slice_name =
          checkpoint::EncodeTensorNameSlice(full_tensor_key_string,
                                            stored_slice);
      status_ =
          GetBundleEntryProto(encoded_stored_slice_name, &stored_slice_entry);
      if (!status_.ok()) return status_;
    }

    // TODO(zongheng): should we take an OpKernelContext, so that we can call
    // allocate_temp()?  Note that without major refactorings to Saver, it's
    // hard for the caller of the tensor bundle module to allocate these
    // precisely-shaped scratch storage.

    // Optimization for the common case: the stored slice can be directly
    // copied to the destination without additional slicing. This is true when
    // either the slices are equal or when they are both full slices having the
    // same shape.
    TensorShape stored_slice_shape(stored_slice_entry.shape());
    if (stored_slice == slice_spec ||
        (stored_slice_shape == val->shape() &&
         IsFullSlice(stored_slice, stored_slice_shape) &&
         IsFullSlice(slice_spec, stored_slice_shape))) {
      VLOG(1) << "Optimized for common case: directly copying into "
                 "pre-allocated buffer; spec: "
              << slice_spec.DebugString();
      status_ = GetValue(stored_slice_entry, val);
      return status_;
    }

    Tensor stored_slice_tensor(stored_slice_entry.dtype(), stored_slice_shape);
    status_ = GetValue(stored_slice_entry, &stored_slice_tensor);
    if (!status_.ok()) return status_;

    // Copies the intersection over.
    const DataType common_dtype = full_tensor_entry.dtype();
    switch (common_dtype) {
#define HANDLE_COPY(T)                                                 \
  case DataTypeToEnum<T>::value:                                       \
    CHECK(CopyDataFromTensorSliceToTensorSlice(                        \
        full_shape, stored_slice, slice_spec,                          \
        stored_slice_tensor.flat<T>().data(), val->flat<T>().data())); \
    break;

      HANDLE_COPY(float)
      HANDLE_COPY(double)
      HANDLE_COPY(int32)
      HANDLE_COPY(uint8)
      HANDLE_COPY(int16)
      HANDLE_COPY(int8)
      HANDLE_COPY(complex64)
      HANDLE_COPY(complex128)
      HANDLE_COPY(int64)
      HANDLE_COPY(bool)
      HANDLE_COPY(qint32)
      HANDLE_COPY(quint8)
      HANDLE_COPY(qint8)
      HANDLE_COPY(bfloat16)
      default:
        return errors::InvalidArgument("Dtype ", DataTypeString(common_dtype),
                                       " not supported.");
    }
#undef HANDLE_COPY
  }
  return Status::OK();
}

bool BundleReader::Contains(StringPiece key) {
  Seek(key);
  return Valid() && (this->key() == key);
}

Status BundleReader::LookupDtypeAndShape(StringPiece key, DataType* dtype,
                                         TensorShape* shape) {
  BundleEntryProto entry;
  TF_RETURN_IF_ERROR(GetBundleEntryProto(key, &entry));
  *dtype = entry.dtype();
  *shape = TensorShape(entry.shape());
  return Status::OK();
}

Status BundleReader::LookupTensorShape(StringPiece key, TensorShape* shape) {
  DataType ignored;
  return LookupDtypeAndShape(key, &ignored, shape);
}

string BundleReader::DebugString() {
  // Format used below emulates that of TensorSliceReader::DebugString().
  string shape_str;
  BundleEntryProto entry;
  Seek(kHeaderEntryKey);
  for (Next(); Valid(); Next()) {
    CHECK(entry.ParseFromArray(value().data(), value().size()));
    if (entry.slices_size() > 0) continue;  // Slice of some partitioned var.

    strings::StrAppend(&shape_str, key(), " (",
                       EnumName_DataType(entry.dtype()), ") ",
                       TensorShape(entry.shape()).DebugString());
    strings::StrAppend(&shape_str, "\n");
  }
  return shape_str;
}

FileOutputBuffer::~FileOutputBuffer() { delete file_; }

Status FileOutputBuffer::Append(StringPiece data) {
  // In the below, it is critical to calculate the checksum on the actually
  // copied bytes, not the source bytes.  This is because "data" typically
  // points to tensor buffers, which may be concurrently written.
  if (data.size() + position_ <= buffer_size_) {
    // Can fit into the current buffer.
    memcpy(&buffer_[position_], data.data(), data.size());
    crc32c_ = crc32c::Extend(crc32c_, &buffer_[position_], data.size());
  } else if (data.size() <= buffer_size_) {
    // Cannot fit, but can fit after flushing.
    TF_RETURN_IF_ERROR(FlushBuffer());
    memcpy(&buffer_[0], data.data(), data.size());
    crc32c_ = crc32c::Extend(crc32c_, &buffer_[0], data.size());
  } else {
    // Cannot fit even after flushing.  So we break down "data" by chunk, and
    // flush/checksum each chunk.
    TF_RETURN_IF_ERROR(FlushBuffer());
    for (size_t i = 0; i < data.size(); i += buffer_size_) {
      const size_t nbytes = std::min(data.size() - i, buffer_size_);
      memcpy(&buffer_[0], data.data() + i, nbytes);
      crc32c_ = crc32c::Extend(crc32c_, &buffer_[0], nbytes);
      position_ = nbytes;
      TF_RETURN_IF_ERROR(FlushBuffer());
    }
    return Status::OK();
  }
  position_ += data.size();
  return Status::OK();
}

Status FileOutputBuffer::AppendSegment(StringPiece data) {
  TF_RETURN_IF_ERROR(FlushBuffer());
  memcpy(&buffer_[0], data.data(), data.size());
  crc32c_ = crc32c::Extend(crc32c_, &buffer_[0], data.size());
  position_ = data.size();
  TF_RETURN_IF_ERROR(FlushBuffer());
  return Status::OK();
}

Status FileOutputBuffer::Close() {
  TF_RETURN_IF_ERROR(FlushBuffer());
  return file_->Close();
}

Status FileOutputBuffer::FlushBuffer() {
  if (position_ > 0) {
    TF_RETURN_IF_ERROR(file_->Append(StringPiece(&buffer_[0], position_)));
    position_ = 0;
  }
  return Status::OK();
}

SegmentBundleWriter::SegmentBundleWriter(BundleWriter* writer,
                                         const string& name,
                                         const TensorShape& shape,
                                         DataType type, int64 buffer_size)
    : writer_(writer),
      name_(name),
      shape_(shape),
      type_(type),
      buffer_size_(buffer_size),
      buffer_(new char[buffer_size]),
      buffer_ptr_(0),
      write_counter_(0) {}

Status SegmentBundleWriter::Begin() {
  return writer_->AddTensorHeader(name_, type_, shape_);
}

Status SegmentBundleWriter::WriteData(const void* data, int64 size) {
  while (size > 0) {
    if (buffer_ptr_ + size <= buffer_size_) {
      memcpy(buffer_.get() + buffer_ptr_, data, size);
      buffer_ptr_ += size;
      size = 0;
    } else {
      int64 w = buffer_size_ - buffer_ptr_;
      memcpy(buffer_.get() + buffer_ptr_, data, w);
      TF_RETURN_IF_ERROR(
          writer_->AppendSegmentData(buffer_.get(), buffer_size_));
      size -= w;
      data = (const char*)data + w;
      buffer_ptr_ = 0;
      write_counter_++;
    }
  }
  return Status::OK();
}

Status SegmentBundleWriter::End() {
  if (write_counter_ * buffer_size_ + buffer_ptr_ !=
      shape_.num_elements() * DataTypeSize(type_)) {
    return errors::Internal("SegmentBundleWriter write size error");
  }
  if (write_counter_ == 0) {
    return writer_->AddCompeleteData(buffer_.get(), buffer_ptr_);
  } else if (buffer_ptr_ > 0) {
    TF_RETURN_IF_ERROR(writer_->AppendSegmentData(buffer_.get(), buffer_ptr_));
    writer_->EndSegmentData(write_counter_ * buffer_size_ + buffer_ptr_,
                            buffer_ptr_);
    return Status::OK();
  } else {
    writer_->EndSegmentData(write_counter_ * buffer_size_ + buffer_ptr_,
                            buffer_size_);
    return Status::OK();
  }
}

SegmentBundleReader::SegmentBundleReader(BundleReader* reader,
                                         const string& name, int64 offset,
                                         int64 size, int64 buffer_size)
    : reader_(reader),
      name_(name),
      buffer_size_(buffer_size),
      offset_(offset),
      size_(size) {}

Status SegmentBundleReader::Begin() {
  TF_RETURN_WITH_CONTEXT_IF_ERROR(
      reader_->LookupDtypeAndShape(name_, &type_, &shape_), "xx1");
  if (size_ == -1) {
    size_ = shape_.dim_size(0);
  }
  if (offset_ + size_ > shape_.dim_size(0)) {
    return errors::InvalidArgument("SegmentBundleReader offset error");
  }
  int64 xsize = DataTypeSize(type_);
  for (int i = 1; i < shape_.dims(); i++) {
    xsize *= shape_.dim_size(i);
  }
  int64 real_size_ = xsize * size_;
  if (real_size_ < buffer_size_) {
    buffer_size_ = real_size_;
  }
  remain_size_ = real_size_;
  int64 var_offset;
  int64 var_size;
  TF_RETURN_IF_ERROR(
      reader_->GetTensorInfo(name_, &var_size, &file_, &var_offset));
  input_.reset(new io::InputBuffer(file_.get(), buffer_size_));
  TF_RETURN_IF_ERROR(input_->Seek(var_offset + xsize * offset_));
  return Status::OK();
}

const TensorShape& SegmentBundleReader::shape() { return shape_; }

DataType SegmentBundleReader::type() { return type_; }

Status SegmentBundleReader::Read(void* data, int64 size) {
  if (size > remain_size_) {
    return errors::InvalidArgument("SegmentBundleReader Read Exhuasted");
  }
  size_t read_size;
  TF_RETURN_IF_ERROR(
    input_->ReadNBytes(size, (char*)data, &read_size)); // NOLINT (readability/casting)
  remain_size_ -= size;
  return Status::OK();
}

Status SegmentBundleReader::Skip(int64 size) {
  if (size > remain_size_) {
    return errors::InvalidArgument("SegmentBundleReader Read Exhuasted");
  }
  TF_RETURN_IF_ERROR(input_->SkipNBytes(size));
  remain_size_ -= size;
  return Status::OK();
}
}  // namespace tfplus

