import os

# run 'python scripts/build_fonts.py' at top level

font_dir = "components/lvgl/scripts/built_in_font"
font_dest = "components/fonts"
sizes = [16, 24]

cmd_prot = "lv_font_conv --no-compress --no-prefilter --bpp 4 --size %d --font %s/Montserrat-Medium.ttf -r 0x20-0xFF \
       --font %s/FontAwesome5-Solid+Brands+Regular.woff \
       -r 61441,61448,61451,61452,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650 \
       --format lvgl -o %s/lv_font_rv3_%d.c --force-fast-kern-format"

for sz in sizes:
    cmd = cmd_prot % (sz, font_dir, font_dir, font_dest, sz)
    os.system(cmd)