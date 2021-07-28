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
