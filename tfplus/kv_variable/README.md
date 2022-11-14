## KvVariable

### 背景 
在蚂蚁金服的AI场景里，tensorflow被大量用在推荐系统的建模上，我们会用到大规模的高维、稀疏特征，这些特征是离散输入，通过使用embedding将离散的输入学习嵌入向量，其网络结构一般都是sparse_input -> embedding -> NN。
实际使用中，这些输入特征的维度可能会超过10亿，对应就是embedding的weight维度超过10亿。
而且在在线学习场景里，特征是实时动态变化的，不断会有新的特征维度出现。

### 问题
* 推荐场景中使用的高维、稀疏特征，且在在线学习、增量学习的场景中，输入的特征是不断变化的，现有的tensorflow Variable不能有效的支持
* 在线学习中，我们用ftrl优化器来提高模型的稀疏化，然而它没考虑参数group的概念，在包含embedding时，这样的稀疏化并不能实现有效的模型稀疏

### Tensorflow当前方案
tensorflow使用hash或vocabulary来对输入特征做编码，再利用embedding_column进行embedding嵌入学习。hash方式code样例如下

```python
sparse_feature = tf.contrib.layers.sparse_column_with_hash_bucket(
        column_name="sparse_col", hash_bucket_size=10e8)
sparse_feature_emb = tf.feature_column.embedding_column(sparse_id_column=sparse_feature, dimension=64)
```
上述样例中，tensorflow最终会创建一个tf.Variable, 其shape为 [10e8, 64]

不管是hash，还是vocabulary都不能高效支持高维、稀疏场景，尤其是在线学习上的特征动态变化场景。
  * hash方式，hash_bucket_size的选择很重要，设置大了会浪费内存；设置小了会出现hash冲突，无关的特征会关联到一起
  * vocabulary方式，需要进行预处理，当数据量超大时，预处理会非常耗时；而且该方式无法处理新增特征的case

## 目标
Tensorflow提供的Variable内部使用Tensor，使用密矩阵的方式进行存储和访问，需要提前申请好shape。

我们实现了一个全新的Variable，取名KvVariable。其内部数据类型采用key-value格式存储(比如使用hash map结构),
并支持以下功能：
 * KvVariable不需预先设定dim0, 只需要embedding_dimension，新出现的特征会被动态加入到训练中，并支持频次过滤
 * KvVariable主要用于embedding的计算，它能支持embedding_lookup的前向、反向计算，能支持所有优化器进行参数更新
 * KvVariable支持PartitionedVariable来进行对超大的embedding进行shard，partition_strategy使用mod,对于string类型，先hash，后mod。
 * KvVariable支持模型save、restore，兼容checkpoint、savedmodel格式，支持导出时自动裁剪稀疏向量
 * 在线学习场景中，新增了group lasso、sparse group lasso两个优化器，结合考虑参数group来更好的学习相关特征，不相关的特征会在训练中被动态删除，有效提供模型的稀疏化，既减少了内存占用，又对模型的泛化性能效果有提升。目前新增了GroupFtrl、GroupAdam两个优化器，分别为Ftrl和Adam增加了group lasso和sparse group lasso的功能支持
 * 提供两种High Level，支持KvVariable学习embedding
    * 新增一个feature_column：kv_embedding_column
    * 新增Keras Layer：KvEmbedding/KvSparseEmbedding
 * KvVariable需要支持tf Eager
 
## 设计方案
1. Add new KvVariable OP C++ Kernel
  
   继承ResourceBase,实现KvVariable，内部使用KV存储，当前使用了std::unordered_map<K, std::unique_ptr<V[]>>数据结构。Key是特征，V[]存储embedding的value，主要是Eigen::Tensor的data。内部
   使用使用Eigen::Tensor, 避免使用looping，保持实现的简洁与高效。

   KvVariable支持接口主要如下
   ```c++
      // Use the input random_init tensor to set the initialization table.
      virtual Status InitRandomValues(const Tensor& random_init) = 0;

      // Get the shape of intialilzation table. If the initialization table
      // has not been set yet, return an error.
      virtual Status GetInitTableShape(TensorShape* shape) const = 0;

      // Copy the content of the initialization table. The input tensor object
      // must have the same shape with the initialization table. We can invoke
      // GetInitTableShape(...) first to get the shape of the initialization table
      // and then allocate a tensor as the input to this function.
      virtual Status GetInitTable(Tensor* tensor) const = 0;

      // FindOrZeros retrieves the tensor with given keys. All zeros will be
      // returned if the given key doesn't exist in the table_. The function is
      // typically used in KvVariableGather OP for model prediction.
      virtual Status FindOrZeros(const Tensor& keys, Tensor* values) const = 0;

      //  FindOrInsert is typically used in KvVariableGather OP for model training.
      //  If the key already in the table_, we return its value, otherwise we will
      //  insert a random vector from random_init_table_ as its initializer vector.
      //  The 3rd param filter_out is used to filter out the low frequency. If the
      //  key is low frequency, we do not insert it, and do not update its gradient
      //  in backward computing.
      virtual Status FindOrInsert(const Tensor& keys, Tensor* values,
                                  const Tensor* filter_out) = 0;

      // InsertOrUpdate is used after optimizer complete the backward computing.
      // The filter_out are low frequency keys which will not be updated.
      // The blacklist are used in group lasso && sparse group lasso optimizer that
      // indicate the key has already group sparse and can be removed.
      virtual Status InsertOrUpdate(const Tensor& keys, const Tensor& values,
                                    const Tensor* filter_out,
                                    const Tensor* blacklist) = 0;

      // Load values from checkpoint or savedmodel.
      virtual Status ImportValues(OpKernelContext* ctx, const Tensor& keys,
                                  const Tensor& values,
                                  const std::vector<Tensor>& others) = 0;

      // Export values to checkpoint or savedmodel.
      // If enable_cutoff is true, we will not export key which all values are
      // smaller than cutoff_value.
      virtual Status ExportValues(OpKernelContext* ctx, int first_n,
                                  bool enable_cutoff = false,
                                  float cutoff_value = 0.0) const = 0;

      virtual Status ExportValuesNoCopy(OpKernelContext* ctx, int first_n,
                                  bool enable_cutoff = false,
                                  float cutoff_value = 0.0,
                                  const string& tensor_key,
                                  BundleWriter* writer) const = 0;
   ```

   主要注册OP的接口有:
   ```c++
   KvVariableV2
   KvVariableShape
   InitKvVariableV2
   KvVariableIsInitializedV2
   KvVariableSizeV2
   ReadKvVariableOpV2
   DestroyKvVariableOpV2
   KvVariableGatherV2
   KvVariableInsertV2
   KvVariableIncreaseCountV2
   KvVariableImportV2
   KvVariableExportV2
   KvVariableScatterAddV2
   KvVariableScatterSubV2
   KvVariableScatterMulV2
   KvVariableScatterDivV2
   KvVariableScatterMinV2
   KvVariableScatterMaxV2
   KvVariableScatterUpdateV2
   ```
2. Add new KvVariable python class
   
   继承ResourceVariable，override相关function，提供python访问KvVariable的接口，内部使用KvVariable相关算子进行访问，并提供Save、Restore的实现
   ```python
      from tensorflow.python.ops import resource_variable_ops
      class KvVariable(resource_variable_ops.ResourceVariable):
        pass
      class KvVariableSaveable(BaseSaverBuilder.SaveableObject):
        pass
   ```

   除此之外，需要修改variable.proto,增加一个字段来判断是否KvVariable类型，在resource_variable_ops.from_proto中进行判断，通过variable_def构造KvVariable类型

3. Add get_kv_variable in tensorflow/python/ops/variable_scope.py
   
   新增tf.get_kv_variable low level api, 支持variable_scope和partitioner
   ```python
      def get_kv_variable(name,
                          embedding_dim=None,
                          key_dtype=dtypes.int64,
                          value_dtype=dtypes.float32,
                          initializer=None,
                          regularizer=None,
                          trainable=None,
                          collections=None,
                          partitioner=None,
                          constraint=None,
                          enter_threshold=0):
   ```
4. Add KvVariable support for all optimizers
   
   KvVariable只是为了解决大规模sparse embedding问题，我们会修改所有优化器的SparseApply c++ OP，支持这些优化器从KvVariable中查找和更新。

   同时需要修改optimizer._get_processor支持KvVariableV2复用_DenseResourceVariableProcessor、slot_creator._create_slot_var来支持KvVariable类型

   新增加GroupFtrl、GroupAdam优化器OP，用来支持group lasso和sparse group lasso算法。

5. tf.nn.embedding_lookup和tf.nn.embedding_lookup_sparse改动
   * 修改embedding_lookup_sparse当前调用array_ops.unique(ids)，如果dtype不为string时，改为调用array_ops.unique_with_counts,返回频次信息。 再调用KvVariableIncreaseCountV2在PS端统计特征频次。
   * 修改embedding_lookup, 增加对KvVariable的处理逻辑，KvVariable的partition_strategy使用mod策略。当dtype为string时，先hash，再mod。

6. low level api example
    ```python
    import tensorflow as tf
    import os

    ckp_dir = './checkpoint'
    ckp_path = os.path.join(ckp_dir, 'model.ckpt')

    num_shards = 50
    with tf.variable_scope('test', reuse=tf.AUTO_REUSE):
      var = tf.get_kv_variable("kv_embedding",
                              embedding_dim=64,
                              key_dtype=tf.int64,
                              initializer=tf.random_normal_initializer(),
                              partitioner=tf.fixed_size_partitioner(num_shards=num_shards))

    emb = tf.nn.embedding_lookup(var, tf.convert_to_tensor([1, 2, 3,6,8,9], dtype=tf.int64))
    emb1 = tf.nn.embedding_lookup(var, tf.convert_to_tensor([1000000000000000], dtype=tf.int64))
    fun = tf.add(emb, 1.0, name='add')
    loss = tf.reduce_sum(fun)
    opt = tf.train.FtrlOptimizer(0.005,
                                l1_regularization_strength=0.025,
                                l2_regularization_strength=0.00001)
    g_v = opt.compute_gradients(loss)
    train_op = opt.apply_gradients(g_v)
    init = tf.global_variables_initializer()
    saver = tf.train.Saver()

    with tf.Session() as sess:
      sess.run(init)
      print(sess.run({'emb':emb, 'fun':fun, 'train': train_op}))
      print(sess.run(emb1))
      save_path = saver.save(sess, ckp_path)
      print'model saved in file %s' % save_path)

    with tf.Session() as sess:
      saver.restore(sess, ckp_path)
      print(sess.run(emb1))
      print(sess.run({'emb':emb, 'fun':fun}))
      print(sess.run(emb1))
    ```
7. FeatureColumn
   
   TODO
8. Keras Layer
   
   TODO
## 待进一步讨论

本文档所提的所有接口和功能已经支持TF 1.13和TF 1.14，但暂时没对TF2.0进行接口设计

我们的设计在C++实现上可以适配TF2.0，目前没有看到会修改Opkernel的接口，但python api可能会有相关的重构

社区现在有几个RFC和variable、optimizer接口有关，有的已经被accept。相关RFC如下
[embedding-partitioned-variable](https://github.com/tensorflow/community/blob/master/rfcs/20190116-embedding-partitioned-variable.md)、[variables](https://github.com/tensorflow/community/blob/master/rfcs/20180817-variables-20.md)、[optimizer-unification](https://github.com/tensorflow/community/blob/master/rfcs/20181016-optimizer-unification.md)。这些RFC的变动也会影响我们在对接TF2.0时对python api的改动
