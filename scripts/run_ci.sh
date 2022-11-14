#!/usr/bin/env bash
set -e
set -x

function usage()
{
    echo "------------ci commands-----------------------"
    echo "usage: run_ci.sh help|ut|package|pylint|cpplint"    
    echo "         help          print this hekp"
    echo "         ut            do unittest"
    echo "         py_ut         do python unittest"
    echo "         cpp_ut        do c++ unittest"
    echo "         package       do package"
    echo "         pylint        do pylint "
    echo "         cpplint       do cpplint"
    echo "----------------------------------------------"
}

function init()
{
    pushd $project_dir
        rm -f build_lib.sh*
        rm -f resolve_ut_result.py*
        rm -f utContent
     	rm -rf result_html
        rm -rf htmlcov
    	rm -rf .tmp_output* 
		wget http://aivolvo-dev.cn-hangzhou-alipay-b.oss-cdn.aliyun-inc.com/citools/build_lib.sh && chmod 777 build_lib.sh
        wget http://aivolvo-dev.cn-hangzhou-alipay-b.oss-cdn.aliyun-inc.com/citools/resolve_ut_result.py && chmod 777 resolve_ut_result.py
        source  build_lib.sh
        wget  https://alps-common.oss-cn-hangzhou-zmf.aliyuncs.com/users/xuantai/bazel-0.24.1-installer-linux-x86_64.sh   && chmod 777 bazel-0.24.1-installer-linux-x86_64.sh
        ./bazel-0.24.1-installer-linux-x86_64.sh
        rm -rf testresult/*
        mkdir -p  testresult
        # tensorflow==1.13.1 已经打在镜像里
        # pip install tensorflow==1.13.1 -i https://pypi.antfin-inc.com/simple
        # 基础镜像直接source ~/.bashrc由于系统设置，无法生效，所以直接执行
#        . /miniconda/etc/profile.d/conda.sh
#        source activate tfplus-dev 

    popd
}

function gen_result_md()
{
    #python resolve_ut_result.py testresult
    echo "python resolve_ut_result.py testresult"
    [[ $? != 0 ]] && exit 1
    return 0 
}

function do_py_pai_ut()
{
    ut_test_dir="py_ut"
    pushd $project_dir
        yum install -b current -y git cmake alicpp-gcc492-glog &&
        ln -sf /apsara/alicpp/built/gcc-4.9.2/glog-0.3.4/include/glog /usr/include/glog &&
        ln -sf /apsara/alicpp/built/gcc-4.9.2/glog-0.3.4/lib/libglog.a /usr/lib64/libglog.a 
        alias python=python3
        alias pip=pip3
        "yes" | mv WORKSPACE.pai WORKSPACE 
        "yes" | cp ./dev/pai-tf1.15/.bazelrc .bazelrc  
        pip3 install protobuf==3.6.1
        pip3 install https://alps-common.oss-cn-hangzhou-zmf.aliyuncs.com/users/xuantai/ps_fo/deeprec/cpu/tensorflow-1.15.5%2Bdeeprec2201-cp36-cp36m-linux_x86_64.whl
		pip3 install scipy
        pip3 install pytest==7.0.1
        pip3 install pytest-cov==3.0.0
        bazel build  -s --verbose_failures ${coverage_params} //tfplus/...
        python setup.py bdist_wheel
        pip install -U dist/*.whl
        rm ./tfplus/__init__.py
        GET_TIME_BEGIN "py_ut"
        echo "ut begin"  > testresult/goingresult.log
        # 为控制台输出能看到,增加下面的tail
        tail -F testresult/goingresult.log &
        # python -m pytest -s -v --cov-report=html --cov=$project_dir  "$ut_test_dir" >  testresult/goingresult.log 2>&1
        set +e
        #python -m pytest -s -v   "$ut_test_dir" >  testresult/goingresult.log 2>&1
        cd $ut_test_dir
        # find . | grep -E "(__pycache__|\.pyc|\.pyo$)" | xargs rm -rf
        pytest -s -v --import-mode=importlib --cov-report=html --cov=$project_dir  "./" >  ../testresult/goingresult.log 2>&1; status=$?
        cd ..
	    cp -r htmlcov testresult/
        ps -ef | grep tail | grep goingresult.log | grep -v grep |awk '{print $2}' | xargs -r -t kill -9
        GET_TIME_END "py_ut"
        gen_result_md
    popd
    return $status
}

function do_py_ut()
{
    ut_test_dir="py_ut"
    pushd $project_dir
        source /home/Miniconda/etc/profile.d/conda.sh
        conda activate myenv
        export PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python
        pip install -U https://alps-common.oss-cn-hangzhou-zmf.aliyuncs.com/users/xuantai/tensorflow-1.15.5-cp37-cp37m-linux_x86_64.whl
        python config_helper.py --use_origin_tf
    	pip install pytest 
		pip install scipy
        pip install pytest==7.0.1
        pip install pytest-cov==3.0.0
        bazel build  -s --verbose_failures ${coverage_params} //tfplus/...
        bazel build  -s --verbose_failures ${coverage_params} --build_tag_filters=-gpu -- //tfplus/... -//tfplus/graph_opt/fuse/...
        python setup.py bdist_wheel
        pip install -U dist/*.whl
        export USE_ORIGIN_TF=true
        rm ./tfplus/__init__.py
        GET_TIME_BEGIN "py_ut"
        echo "ut begin"  > testresult/goingresult.log
        # 为控制台输出能看到,增加下面的tail
        tail -F testresult/goingresult.log &
        # python -m pytest -s -v --cov-report=html --cov=$project_dir  "$ut_test_dir" >  testresult/goingresult.log 2>&1
        set +e
        #python -m pytest -s -v   "$ut_test_dir" >  testresult/goingresult.log 2>&1
        cd $ut_test_dir
        # find . | grep -E "(__pycache__|\.pyc|\.pyo$)" | xargs rm -rf
        pytest -s -v --import-mode=importlib --cov-report=html --cov=$project_dir  "./" >  ../testresult/goingresult.log 2>&1; status=$?
        cd ..
	    cp -r htmlcov testresult/
        ps -ef | grep tail | grep goingresult.log | grep -v grep |awk '{print $2}' | xargs -r -t kill -9
        GET_TIME_END "py_ut"
        gen_result_md
    popd
    return $status
}

function do_py_ut_gpu()
{

    ut_test_dir="py_ut_gpu"
    pushd $project_dir
        echo "build tfplus in eflops mode"
        echo "tfplus/aistudio_reader" > .bazelignore
        bash scripts/build_eflops.sh
        pip install dist/*.whl -I
        GET_TIME_BEGIN "py_ut_gpu"
        echo "ut begin"  > testresult/goingresult.log
        tail -F testresult/goingresult.log &
        set +e
        # run test indivaildlly to avoid CUDA_VISIBLE_DEVICES error
        for i in `ls $ut_test_dir/test_*`;do
            pytest -s -v --cov-report=html --cov=$project_dir $i >>  testresult/goingresult.log 2>&1
        done
        status=$?
	    cp -r htmlcov testresult/
        ps -ef | grep tail | grep goingresult.log | grep -v grep |awk '{print $2}' | xargs -r -t kill -9
        GET_TIME_END "py_ut_gpu"
        gen_result_md
    popd
    return $status
}

function do_cpp_ut(){
    pushd $project_dir
        GET_TIME_BEGIN "cpp_ut"
        source /home/Miniconda/etc/profile.d/conda.sh
        conda activate myenv
        export PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python
        pip install -U https://alps-common.oss-cn-hangzhou-zmf.aliyuncs.com/users/xuantai/tensorflow-1.15.5-cp37-cp37m-linux_x86_64.whl
        python config_helper.py --use_origin_tf
        echo "ut begin"  > testresult/goingresult.log
        tail -F testresult/goingresult.log &
        set +e
        bazel test --test_tag_filters=-gpu -- //tfplus/...  -//tfplus/graph_opt/fuse/... >  testresult/goingresult.log 2>&1
        status=$?
        ps -ef | grep tail | grep goingresult.log | grep -v grep |awk '{print $2}' | xargs -r -t kill -9
        GET_TIME_END "cpp_ut"
#        get_cpp_coverage
        cp -r result_html testresult
        gen_result_md
    popd
    return $status
}

function get_cpp_coverage(){
  path="bazel-testlogs/"
  datfiles=`find ${path} -name "coverage.dat"`
  root_path=`pwd`'/'
  sed -i "s%^SF:%SF:${root_path}%g" ${datfiles}
  for file in ${datfiles}
  do
    mv ${file} ${file}.bk
    lcov --remove ${file}.bk '*/third_party/*' '*.pb.h' '*.pb.cc' -o ${file}
    chmod 777 ${file}
  done
  find ${path} -name "coverage.dat" -size 0 -delete
  datfiles=`find ${path} -name "coverage.dat"`
  genhtml --ignore-errors "source" ${datfiles} -o result_html
}


function do_pylint(){
    pushd $project_dir
       # find . -name '*.py' | xargs pylint --load-plugins pylint_quotes --ignore=resolve_ut_result.py  -j 4 --rcfile=.pylint --output-format=text -r y
       find . -name '*.py' -not -path "./tfplus/data/tf_example/*" -not -path "./third_party/*" \
           | xargs pylint --ignore=resolve_ut_result.py  -j 4 --rcfile=.pylint --output-format=text -r y
    popd
}

function do_cpplint(){
    cpp_check_dir="tfplus"
    pushd $project_dir
        cpplint --exclude tfplus/graph_opt/pattern/all_to_all_pattern.cc --recursive $cpp_check_dir
    popd
}

function do_package(){
    pushd $project_dir
        bazel build  -s --verbose_failures ${coverage_params} //tfplus/...
        python setup.py bdist_wheel
    popd
}

current_dir=$(cd "$(dirname "${BASH_SOURCE:-$0}")"; pwd)
project_dir=$(dirname $current_dir)
if [ $# -eq 0 ]; then
    usage
    exit 0
fi
init
case $1 in
    help|-h|-help|--help)
        usage
    ;;
    ut)
        do_cpp_ut
        do_py_ut
        py_pai_ut
        do_py_ut_gpu
    ;;
    py_ut)
        do_py_ut
    ;;
    py_pai_ut)
        do_py_pai_ut
    ;;
    py_ut_gpu)
        do_py_ut_gpu
    ;;
    cpp_ut)
        do_cpp_ut
    ;;
    package)
        do_package
    ;;
    pylint)
        do_pylint
    ;;
    cpplint)
        do_cpplint
    ;;
    package)
        do_package
    ;;
esac
