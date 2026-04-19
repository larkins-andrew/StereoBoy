if ! git checkout $1 2>&1 | grep -q 'error'; then
    git checkout main
    git tag archive/$1 $1
    git push origin archive/$1
    git branch -D $1
    git branch -d -r origin/$1
    git push -d origin $1
else
    echo "Error: Failed to check out $1"
fi


