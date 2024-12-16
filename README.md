# ParallelThreads
Modified given serial.c file to implement threading in order to improve the runtime of the compression while maintaining the same output as serial.c.  Original serial.c file compresses a directory of .ppm files with zlib functions at max (9) compression value. Takes one argument at commandline: the directory that the ppm files are at.  Output hardcoded as "video.vzip".
<br>
Requires being compiled in a GNU environment for access to pthreads.h library.  Tested with gcc version 11.4.0 on Ubuntu 11.4.0-1ubuntu1 22.04.
<br>
![Gradescope showing 5.5x faster execution time](/Screenshot_Gradescope.png)
<br>
Achieves average of 5.5x faster execution compared to serial compression, lows around 4.9x and highs around 6.3x.  Measured by Gradescope, which runs the code on a virtualized 4-core cpu with 6GB of RAM.  
