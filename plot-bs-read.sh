#! /bin/sh

gnuplot <<EOF > plot-bs-read.png
set term png size 1920,1080 small  
set autoscale
set title "bw"
set boxwidth 0.4  
set grid
set xlabel "block size"
set ylabel "iops"  
set y2label "bytes/s"
set y2tics
set logscale x 2
plot "plot-bw-iop.dat" using 1:4 axes x1y1 with lines title 'iops' \
,    "plot-bw-iop.dat" using 1:5 axes x1y2 with lines title 'bytes/s'
EOF
