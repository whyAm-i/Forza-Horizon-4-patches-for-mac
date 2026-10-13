#!/bin/bash
# Run from Finder or Terminal. All game tests use the existing live skip list.
TASK_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export PYTHONPYCACHEPREFIX=/private/tmp/forza-shader-lab-python-cache
while true; do
    echo
    echo 'Forza Shader Lab'
    echo '1) Review all loaded shaders (resume saved progress)'
    echo '2) Review vertex shaders only (object identification)'
    echo '3) Find a target object (small groups, manual restoration)'
    echo '4) Refresh the shader inventory after visiting more scenes'
    echo '5) Test one shader hash'
    echo '6) Restore skip.txt after an interrupted test'
    echo 'q) Quit'
    read -r -p 'Choose: ' choice || exit
    case "$choice" in
        1) python3 "$TASK_DIR/shader_lab.py" review --stage all --refresh ;;
        2) python3 "$TASK_DIR/shader_lab.py" review --stage vs --refresh ;;
        3) python3 "$TASK_DIR/shader_lab.py" bisect --stage vs --refresh ;;
        4) python3 "$TASK_DIR/shader_lab.py" inventory ;;
        5)
            read -r -p 'Shader stage (vs/ps/gs/hs/ds/cs): ' shader_stage || exit
            read -r -p 'Eight-digit shader hash: ' shader_hash || exit
            python3 "$TASK_DIR/shader_lab.py" review --stage "$shader_stage" --shader "$shader_hash" --refresh --retest
            ;;
        6) python3 "$TASK_DIR/shader_lab.py" restore ;;
        q|Q) exit ;;
        *) echo 'Choose 1-6 or q.' ;;
    esac
done
