## Screenshot

![Alt text](screenshot.png?raw=true "Screenshot")

## How to build

    make

    sudo make install
    (installs to /usr/local/bin/)

## How to use

    git diff | nadiff

    git diff --staged | nadiff

    git diff HEAD~1..HEAD | nadiff

    You get the point.


## How to navigate

    n Next diff
    N Previous diff
    a Scroll up in left diff view
    z Scroll down in left diff view
    s Scroll up in right diff view
    x Scroll down in right diff view
    d Scroll up in both left and right diff view
    c Scroll down in both left and right diff view
    q Quit
