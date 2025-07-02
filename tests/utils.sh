function ENTRY
{
    echo "## ${FUNCNAME[1]}"
}

function exit_error
{
    echo "$*"
    exit 1
}

function diff_with_rm_on_err
{
    diff $1 $2 || { rm $1; exit_error $3; }
}

function invoke_lrs_debug()
{
    # XXX grep --line-buffered allows each line to be written in EVENT_FILE as
    # they are read in the logs. Otherwise, the logs would not be always
    # flushed.
    stdbuf -eL -oL $phobosd -vv -i 2>&1 | stdbuf -i0 -oL tr -d '\000' |
        grep --line-buffered "sync: " > "$EVENT_FILE"
}

function now()
{
    date +%s%N
}

function get_timestamp()
{
    echo "$1" | awk '{print $1, $2}' | xargs -I{} date -d "{}" +%s%N
}

function newer()
{
    local t=$1

    while read line; do
        if [ $t -le $(get_timestamp "$line") ]; then
            echo "$line"
        fi
    done
}

function get_daemon_event()
{
    # get new events
    local event="$1: "

    grep "$event" "$EVENT_FILE" | newer $last_read
}

function wait_for_lrs()
{
    local Nretry=10
    local N=0

    while ! $phobos phobosd ping; do
        local N=$((N+1))

        if [ $N -ge $Nretry ]; then
            exit 1
        fi

        sleep 0.1
    done
}
