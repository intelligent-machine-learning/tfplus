| 名称                                                          | 描述                  |
| reg.docker.alibaba-inc.com/penrose/py37_cuda11.2_paitf115:v1 | python3.7 pai tensorflow 1.15 编译镜像  |



### tensorflow cpu版本构建方法
#### 代码拉取和修改

注意
git 版本大于2.23.0

```bash
git clone http://gitlab.alibaba-inc.com/algo/tensorflow.git
cd tensorflow 
git checkout features/r1.15_tfplus
```
or
```bash
git clone https://code.alipay.com/Arc/tensorflow.git 
cd tensorflow 
git checkout r1.15_paitf
```

```bash
for v in $(set|grep ^TF_|awk -F= '{print $1}'); do unset $v; done
export TF_BUILD_PYTHON_VERSION=python3.7
tools/rename_protobuf.sh protobuf
wget http://tfsmoke1.cn-hangzhou.oss.aliyun-inc.com/bazel-0.24.1-installer-linux-x86_64.sh
sh bazel-0.24.1-installer-linux-x86_64.sh
./configure # 区分cpu 和 gpu 版本
```
修改 .tf_configure.bazelrc ，新增加以下内容
```
build --cxxopt="-D_GLIBCXX_USE_CXX11_ABI=0" --host_cxxopt="-D_GLIBCXX_USE_CXX11_ABI=0"
```

####  编译python包
```bash
bazel build --cxxopt="-D_GLIBCXX_USE_CXX11_ABI=0" --host_cxxopt="-D_GLIBCXX_USE_CXX11_ABI=0" -c opt --config=opt --config=mkl_threadpool --define build_with_mkl_dnn_v1_only=true //tensorflow/tools/pip_package:build_pip_package # cpu 版本
bazel build -c opt --config=opt --config=nogcp --config=noaws  --config=cuda //tensorflow/tools/pip_package:build_pip_package # gpu 版本 
./bazel-bin/tensorflow/tools/pip_package/build_pip_package /tmp/tensorflow_pkg
```


