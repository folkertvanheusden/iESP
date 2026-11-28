width = 72.8 + 10 /* width margin */;
depth = 21;
height = 30;
inner_margin = 0.1;
walls_thick = 5;
lid_thick = walls_thick;
led_dia = 10;
display_width = 40;
display_depth = 12;
ethernet_depth = 16;
ethernet_height = 28;
difference() {
	cube([ width + inner_margin * 2 + walls_thick * 2, depth + inner_margin * 2 + walls_thick * 2, height + inner_margin * 2 + walls_thick * 2]);
    // inner
	translate([walls_thick + inner_margin, walls_thick + inner_margin, walls_thick + inner_margin])
		cube([ width, depth, height + 100]);
    // Ethernet connector
    translate([-1, depth / 2 - walls_thick / 2, walls_thick]) cube([50, ethernet_depth, ethernet_height]);
}


translate([100, 0, lid_thick])
difference() {
    union() {
        cube([width + inner_margin * 0.75, depth + inner_margin * 0.75, lid_thick]);
        translate([-walls_thick, -walls_thick, -lid_thick]) cube([width + inner_margin * 2 + walls_thick * 2, depth + inner_margin * 2 + walls_thick * 2, lid_thick]);
    };
   

   // leds
   translate([walls_thick + 2, depth / 2 + inner_margin, 0])
   union() {
       translate([0, 0, 0]) cylinder(d = led_dia, h=100, center = true);
       translate([1 * led_dia + 1, 0, 0]) cylinder(d = led_dia, h=100, center = true);
       translate([2 * led_dia + 2, 0, 0]) cylinder(d = led_dia, h=100, center = true);
   }
   
   // display
   translate([walls_thick + 3 * led_dia + 4
   , depth / 2 - display_depth / 2, -walls_thick * 2])
   union() {
        cube([display_width, display_depth, 100]);
   }
}