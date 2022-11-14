
workspace(name = "org_tfplus")

load("//third_party:repositories.bzl", "prepare_six")
load("//third_party:repo.bzl", "tf_http_archive")
load("//third_party:repo.bzl", "clean_dep")
load("//third_party/tf:tf_configure.bzl", "tf_configure")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

tf_configure(name = "local_config_tf")
prepare_six()

http_archive(
      name = "com_google_googletest",
      strip_prefix = "googletest-release-1.8.1",
      sha256 = "9bf1fe5182a604b4135edc1a425ae356c9ad15e9b23f9f12a02e80184c3a249c",
      urls = ["http://aivolvo-dev.cn-hangzhou-alipay-b.oss-cdn.aliyun-inc.com/common/googletest/googletest-release-1.8.1.tar.gz"],
)

http_archive(
    name = "farmhash",
    sha256 = "6560547c63e4af82b0f202cb710ceabb3f21347a4b996db565a411da5b17aba0",
    build_file = "//third_party:farmhash.BUILD",
    strip_prefix = "farmhash-816a4ae622e964763ca0862d9dbd19324a1eaf45",
    urls = [
        "https://mirror.bazel.build/github.com/google/farmhash/archive/816a4ae622e964763ca0862d9dbd19324a1eaf45.tar.gz",
        "https://github.com/google/farmhash/archive/816a4ae622e964763ca0862d9dbd19324a1eaf45.tar.gz",
    ],
)

http_archive(
    name = "com_github_madler_zlib",
    build_file = "//third_party:zlib.BUILD",
    sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
    strip_prefix = "zlib-1.2.11",
    urls = [
        "https://mirror.bazel.build/zlib.net/zlib-1.2.11.tar.gz",
        "https://zlib.net/zlib-1.2.11.tar.gz",
    ],
)

http_archive(
    name = "tbb",
    build_file = "//third_party:tbb.BUILD",
    sha256 = "c3245012296f09f1418b78a8c2f17df5188b3bd0db620f7fd5fabe363320805a",
    strip_prefix = "tbb-2019_U1",
    urls = [
        "http://alps-common.oss-cn-hangzhou-zmf.aliyuncs.com/2019_U1.zip",
        "https://github.com/01org/tbb/archive/2019_U1.zip",
    ],
)

http_archive(
    name = "sparsehash",
    build_file = "//third_party:sparsehash.BUILD",
    sha256 = "d4a43cad1e27646ff0ef3a8ce3e18540dbcb1fdec6cc1d1cb9b5095a9ca2a755",
    strip_prefix = "sparsehash-c11-2.11.1",
    urls = [
        "http://lvshan-public.oss-cn-hangzhou-zmf.aliyuncs.com/feisheng/package/sparsehash/v2.11.1.tar.gz",
        "https://github.com/sparsehash/sparsehash-c11/archive/v2.11.1.tar.gz"],
)

http_archive(
    name = "murmurhash",
    build_file = "//third_party:murmurhash.BUILD",
    sha256 = "19a7ccc176ca4185db94047de6847d8a0332e8f4c14e8e88b9048f74bdafe879",
    strip_prefix = "smhasher-master",
    urls = [
        "http://lvshan-public.oss-cn-hangzhou-zmf.aliyuncs.com/feisheng/package/murmurhash/master.zip",
        "https://github.com/aappleby/smhasher/archive/master.zip"],
)

http_archive(
    name = "boost_archive",
    urls = [
        "https://boostorg.jfrog.io/artifactory/main/release/1.74.0/source/boost_1_74_0.tar.bz2",
    ],
    build_file = "//third_party/boost:boost.BUILD",
    patch_args = ["-p1"],
    patches = [
        "//third_party/boost:spsc.patch",
    ],
    strip_prefix = 'boost_1_74_0',
    sha256 = "83bfc1507731a0906e387fc28b7ef5417d591429e51e788417fe9ff025e116b1"
)

new_git_repository(
    name = "com_antfin_libzdfs",
    branch = "v2.1.0p1",
    patch_args = ["-p1"],
    remote = "https://git:88f7becdc16a7b1925d327014faf43@code.alipay.com/ims/zdfs.git",
    patches = [
        "//third_party:libzdfs-patch-CMakeLists.patch",
    ],
    build_file = "//third_party:libzdfs.BUILD",
)

http_archive(
    name = "com_libarchive",
    urls = ["http://arcos.oss-cn-hangzhou-zmf.aliyuncs.com/mochen/jianmu/libarchive-3.6.1.tar.gz"],
    build_file = "//third_party:libarchive.BUILD",
)


http_archive(
    name = "aliyun_oss_c_sdk",
    build_file = "//third_party:oss_c_sdk.BUILD",
    sha256 = "6450d3970578c794b23e9e1645440c6f42f63be3f82383097660db5cf2fba685",
    strip_prefix = "aliyun-oss-c-sdk-3.7.0",
    urls = [
        "http://lvshan-public.oss-cn-hangzhou-zmf.aliyuncs.com/feisheng/package/aliyun-oss-c-sdk-3.7.0.tar.gz",
        "https://github.com/aliyun/aliyun-oss-c-sdk/archive/3.7.0.tar.gz",
    ],
)

http_archive(
    name = "mxml",
    build_file = "//third_party:mxml.BUILD",
    patch_args = ["-p1"],
    patches = [
        "//third_party:mxml.patch",
    ],
    sha256 = "4d850d15cdd4fdb9e82817eb069050d7575059a9a2729c82b23440e4445da199",
    strip_prefix = "mxml-2.12",
    urls = [
        "http://lvshan-public.oss-cn-hangzhou-zmf.aliyuncs.com/feisheng/package/mxml-v2.12.tar.gz",
        "https://github.com/michaelrsweet/mxml/archive/v2.12.tar.gz",
    ],
)

http_archive(
    name = "curl",
    build_file = "//third_party:curl.BUILD",
    sha256 = "e9c37986337743f37fd14fe8737f246e97aec94b39d1b71e8a5973f72a9fc4f5",
    strip_prefix = "curl-7.60.0",
    urls = [
        "https://mirror.bazel.build/curl.haxx.se/download/curl-7.60.0.tar.gz",
        "https://curl.haxx.se/download/curl-7.60.0.tar.gz",
    ],
)

http_archive(
    name = "libaprutil1",
    build_file = "//third_party:libaprutil1.BUILD",
    patch_args = ["-p1"],
    patches = [
        "//third_party:libaprutil1.patch",
    ],
    sha256 = "4c9ae319cedc16890fc2776920e7d529672dda9c3a9a9abd53bd80c2071b39af",
    strip_prefix = "apr-util-1.6.1",
    urls = [
        "http://lvshan-public.oss-cn-hangzhou-zmf.aliyuncs.com/feisheng/package/apr-util/archive/1.6.1.tar.gz",
        "https://github.com/apache/apr-util/archive/1.6.1.tar.gz",
    ],
)

http_archive(
    name = "libapr1",
    build_file = "//third_party:libapr1.BUILD",
    patch_args = ["-p1"],
    patches = [
        "//third_party:libapr1.patch",
    ],
    sha256 = "1a0909a1146a214a6ab9de28902045461901baab4e0ee43797539ec05b6dbae0",
    strip_prefix = "apr-1.6.5",
    urls = [
        "http://lvshan-public.oss-cn-hangzhou-zmf.aliyuncs.com/feisheng/package/apr/archive/1.6.5.tar.gz",
        "https://github.com/apache/apr/archive/1.6.5.tar.gz",
    ],
)

http_archive(
    name = "libexpat",
    build_file = "//third_party:libexpat.BUILD",
    sha256 = "574499cba22a599393e28d99ecfa1e7fc85be7d6651d543045244d5b561cb7ff",
    strip_prefix = "libexpat-R_2_2_6/expat",
    urls = [
        "http://aivolvo-dev.cn-hangzhou-alipay-b.oss-cdn.aliyun-inc.com/common/libexpat/archive/R_2_2_6.tar.gz",
        "https://mirror.bazel.build/github.com/libexpat/libexpat/archive/R_2_2_6.tar.gz",
        "http://github.com/libexpat/libexpat/archive/R_2_2_6.tar.gz",
    ],
)

http_archive(
    name = "boringssl",
    sha256 = "1188e29000013ed6517168600fc35a010d58c5d321846d6a6dfee74e4c788b45",
    strip_prefix = "boringssl-7f634429a04abc48e2eb041c81c5235816c96514",
    urls = [
        "http://lvshan-public.oss-cn-hangzhou-zmf.aliyuncs.com/feisheng/package/boringssl.tar.gz",
        "https://mirror.bazel.build/github.com/google/boringssl/archive/7f634429a04abc48e2eb041c81c5235816c96514.tar.gz",
        "https://github.com/google/boringssl/archive/7f634429a04abc48e2eb041c81c5235816c96514.tar.gz",
    ],
)

http_archive(
    name = "libaprutil1",
    build_file = "//third_party:libaprutil1.BUILD",
    patch_args = ["-p1"],
    patches = [
        "//third_party:libaprutil1.patch",
    ],
    sha256 = "4c9ae319cedc16890fc2776920e7d529672dda9c3a9a9abd53bd80c2071b39af",
    strip_prefix = "apr-util-1.6.1",
    urls = [
        "http://lvshan-public.oss-cn-hangzhou-zmf.aliyuncs.com/feisheng/package/apr-util/archive/1.6.1.tar.gz",
        "https://github.com/apache/apr-util/archive/1.6.1.tar.gz",
    ],
)

http_archive(
    name = "com_google_protobuf",
    sha256 = "2244b0308846bb22b4ff0bcc675e99290ff9f1115553ae9671eba1030af31bc0",
    strip_prefix = "protobuf-3.6.1.2",
    urls = [
        "http://lvshan-public.oss-cn-hangzhou-zmf.aliyuncs.com/feisheng/package/protobuf/v3.6.1.2.tar.gz",
        "https://github.com/protocolbuffers/protobuf/archive/v3.6.1.2.tar.gz"],
)