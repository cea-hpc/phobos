function setup_test_dirs
{
    DIR_TEST="/tmp/$(basename -s .sh ${BASH_SOURCE[1]})"
    DIR_TEST_IN="$DIR_TEST/in"
    DIR_TEST_OUT="$DIR_TEST/out"

    mkdir -p $DIR_TEST $DIR_TEST_IN $DIR_TEST_OUT
}

function cleanup_test_dirs
{
    rm -rf $DIR_TEST
}

function setup_dummy_files
{
    local nb_files=$1
    local i=0

    FILES=()

    set +x
    # do not display file creation commands to reduce the amount of logs
    while [ $i -lt $nb_files ]
    do
        FILES+=("$(mktemp $DIR_TEST_IN/XXXXXX)")
        dd if=/dev/urandom of=${FILES[$i]} bs=1k count=1 &>/dev/null
        i=$((i+1))
    done
    set -x
}

function cleanup_dummy_files
{
    rm -f $FILES
}

function generate_prefix_id
{
    echo "$(basename -s .sh ${BASH_SOURCE[1]})/${FUNCNAME[1]}"
}
