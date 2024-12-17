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
    local bs=$2
    local count=$3
    local i=0

    FILES=()

    set +x
    # do not display file creation commands to reduce the amount of logs
    while [ $i -lt $nb_files ]
    do
        FILES+=("$(mktemp $DIR_TEST_IN/XXXXXX)")
        dd if=/dev/urandom of=${FILES[$i]} bs=${bs} count=${count} &>/dev/null
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

function make_tmp_fs()
{
    local mount_point=$(mktemp -d)
    local file_path=$(mktemp)
    local loop_device
    local unit=${1: -1} # Last char
    local count=${1::-1} # All the string except the last char

    [[ $unit =~ [kKmMgG] ]] ||
        error "Invalid unit $unit"

    if (( count < 60)) && [[ $unit == k || $unit == K ]]; then
        error "Ext4 size should be at least 60K"
    fi

    dd if=/dev/zero of=$file_path count=$count bs=1$unit 2>/dev/null

    loop_device=$(losetup -f)
    losetup $loop_device $file_path

    mkfs.ext4 $loop_device >/dev/null 2>&1

    mkdir -p $mount_point
    mount $loop_device $mount_point

    echo $mount_point
}

cleanup_tmp_fs()
{
    local mount_point=$1
    local loop_device=$(df $mount_point | tail -n 1 | awk '{print $1}')
    local back_file=$(losetup -l -O BACK-FILE $loop_device | tail -n 1)

    umount $mount_point
    rmdir $mount_point
    losetup -d $loop_device
    rm $back_file
}
