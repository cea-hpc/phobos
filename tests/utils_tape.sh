function mtx_retry
{
    local retry_count=0
    while ! mtx $*; do
        echo "mtx failure, retrying in 1 sec" >&2
        ((retry_count++)) | true
        [ $retry_count -gt 5 ] && return 1
        sleep 1
    done
}

function empty_drives
{
    mtx status | grep "Data transfer Element" | grep "Full" |
        while read line; do
            drive=$(echo $line | awk '{print $4}' | cut -d':' -f1)
            slot=$(echo $line | awk '{print $7}')
            mtx unload $slot $drive || echo "mtx failure"
        done
}

function get_tapes
{
    local pattern=$1
    local count=$2

    mtx status | grep VolumeTag | sed -e "s/.*VolumeTag=//" |
        grep "$pattern" | head -n $count | nodeset -f
}

function get_drives
{
    local count=$1

    find /dev -regex "/dev/st[0-9]+$" | head -n $count | xargs
}
