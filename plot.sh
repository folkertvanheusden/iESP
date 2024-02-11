#! /usr/bin/gnuplot

set terminal png size 1920,1080
set output 'plot.png'
set hidden3d
set xyplane at 0
set xlabel 'block size'
set logscale x 2
set ylabel 'number of threads'
set zlabel 'IOPS'
set boxdepth 0.9
set boxwidth 0.9
splot 'plot.dat' with boxes palette
