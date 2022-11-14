# Build and tar so to specific directory
pwd=`pwd`

set -e
bash ${pwd}/scripts/build_only.sh "$@"

bash -c "python ${pwd}/scripts/find_so.py $LINKB_WORKSPACE/so.tar.gz"
