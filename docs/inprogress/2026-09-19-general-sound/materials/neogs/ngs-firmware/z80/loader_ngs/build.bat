
@echo off

if not exist tmp md tmp
call setpath_tools
call setpath_includes

cls

asl -U -L -C -x -olist tmp\loader.lst -i %includes% loader_ngs.a80 || goto error
p2bin loader_ngs.p loader_ngs.rom -r $-$ -k

echo ########################
echo # -= End Compile Ok =- #
echo ########################
goto exit

:error
echo #######################
echo # -= Error Compile =- #
echo #######################

:exit
