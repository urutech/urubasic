#!/bin/bash
total=0
copied=0
diffed=0
failed=0

run_test () {
   filepath=$1
   program=$2
   filespec=$3
   l_total=0
   l_copied=0
   l_diffed=0
   l_failed=0
   str=""

   spec="${filepath}${filespec}"

    for filename in ${spec}; do
        [ -e "$filename" ] || continue
        ((l_total=l_total+1))
        echo "$program <""$filename"
        fname=${filename##*/}

        rm -f "${filename%.*}.res"
        $program < "$filename" > "${filename%.*}.res"

        if [ -f "${filename%.*}.ok" ]; then
            ((l_diffed=l_diffed+1))
        	diff "${filename%.*}.res" "${filename%.*}.ok"

            if [ $? -ne 0 ]; then
                ((l_failed=l_failed+1))
                echo "$filename failed"
            fi
        else
            if [ -f "${filename%.*}.res" ]; then
                ((l_copied=l_copied+1))
        	    cp "${filename%.*}.res" "${filename%.*}.ok"
            elif [[ "${fname}" != E* ]]; then
                ((l_failed=l_failed+1))
                echo "$filename failed"
            fi
        fi
    done

    echo $l_failed "," $l_diffed "," $l_copied "," $l_total >"${filepath}/result.out"
}

read_test_results () {
    filepath=$1

    while IFS=, read -r field1 field2 field3 field4 field5
    do
        ((failed=failed+$field1))
        ((diffed=diffed+$field2))
        ((copied=copied+$field3))
        ((total=total+$field4))
    done < $filepath
}

# run all tests

run_test "test" "./urubasic" "/*.bas"

read_test_results "test/result.out"

# print results
if [ -z "$failed" ]; then
    echo 0 failed, $copied new from $total files
else
    echo $failed failed, $copied new from $total files
fi
