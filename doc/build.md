## Developing

We use Docker container for development. The Dockerfile can be found [here](dev/Dockerfile):

```bash
# Pull Docker image based on tf 1.15.5
sudo docker pull reg.docker.alibaba-inc.com/alipay-alps/alps-runtime:cpu-tf1.13.1-gcc6.5-pre2
# or build locally
docker build -f dev/pai-tf1.15-py37/Dockerfile.cpu -t tfplus-dev .

# Run Docker container and mount source directory to /v
docker run -it --rm --net=host -v ${PWD}:/v -w /v tfplus-dev /bin/bash

# Install tensorflow==1.15.5
pip install -U https://alps-common.oss-cn-hangzhou-zmf.aliyuncs.com/users/xuantai/aistudio_reader_1_tfplus_compile/tensorflow-1.15.5%2Bdeeprec2201-cp37-cp37m-linux_x86_64.whl
# Clone the code 
git clone https://code.alipay.com/Arc/tf-plus.git
cd tf-plus
git checkout tfplus_vnext
# Configure, Build and run unit tests and run c++, python code style check
python config_helper.py 
# build tfplus
bazel build  -s --verbose_failures //tfplus/...
# build and run cpp test
bazel test //tfplus/...
# Build TFPlus package for tf1.15.5
python setup.py bdist_wheel
# Install tfplus first
pip install -U dist/*.whl
# Run python style checks
find tfplus/ -name '*.py' | xargs pylint --rcfile=.pylint
# Run c++ style checks
cpplint --recursive tfplus
# Run python tests, must install tfplus first
pip install -U dist/*.whl
pytest  --import-mode=importlib py_ut
```

