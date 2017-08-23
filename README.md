# vnc-vero3

Forked from samnazarkos and added mousse support 

There is a init in vncserver.c, doptr, that should be in newinput.c, but 
I dont know how to change newinput.h to reflect the changes.

initUinput, changed in newinput.c to accomodate mouse input.
doptr, added to vncserver.c (reason above)
