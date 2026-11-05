# an `if` without its `fi`: the shell must say so and not carry it over
!silenced
if true; then
    echo "dentro lo script rotto"
