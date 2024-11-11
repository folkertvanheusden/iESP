$fa=1;$fs=0.1;

// wt32-eth01
// 2.1 holes
// 2.5 width
// 0.2 holes width

// 1.9 sd reader

module wt32eth01(x, y) {
    translate([x, y-0.15, 0.5]) cylinder(r=0.1, h=0.5, center=true);
    translate([x + 2.1, y-0.15, 0.5]) cylinder(r=0.1, h=0.5, center=true);
}

module sdreader(x, y) {
    // SD card reader
    translate([x + 0.05, y + 7, 0]) cylinder(r=0.1, h=0.5);
    translate([x + 1.95, y + 7, 0]) cylinder(r=0.1, h=0.5);
    translate([x + 0.05, y + 7 + 4.1, 0]) cylinder(r=0.1, h=0.5);
    translate([x + 1.95, y + 7 + 4.1, 0]) cylinder(r=0.1, h=0.5);
}

scale([10, 10, 10]) {
    // ground plate
    difference() {
        cube([15,13,0.3]);
        // holes for wt32-eth01 modules
        color("grey") translate([1-0.2, 1-0.3, 0.235]) cube([2.5, 5.5, 0.2]);
        color("grey") translate([1-0.2 + 3.5, 1-0.3, 0.235]) cube([2.5, 5.5, 0.2]);
        color("grey") translate([1-0.2 + 7, 1-0.3, 0.235]) cube([2.5, 5.5, 0.2]);
        color("grey") translate([1-0.2 + 10.5, 1-0.3, 0.235]) cube([2.5, 5.5, 0.2]);
        // holes or sdcard
        color("blue") translate([1-0.2, 7 + 1-0.3, 0.235]) cube([2.5, 4.7, 0.2]);
        color("blue") translate([1-0.2 + 3.5, 7 + 1-0.3, 0.235]) cube([2.5, 4.7, 0.2]);
        color("blue") translate([1-0.2 + 7, 7 + 1-0.3, 0.235]) cube([2.5, 4.7, 0.2]);
        color("blue") translate([1-0.2 + 10.5, 7 + 1-0.3, 0.235]) cube([2.5, 4.7, 0.2]);
    }
    
    wt32eth01(1, 1);
    sdreader(1, 1);
    wt32eth01(4.5, 1);
    sdreader(4.5, 1);
    wt32eth01(8, 1);
    sdreader(8, 1);
    wt32eth01(11.5, 1);
    sdreader(11.5, 1);
}
