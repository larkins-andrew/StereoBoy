from PIL import Image
from math import floor

characters = ' !"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~'
characters = [c for c in characters]

def genSubImages(filename, grid_width, grid_height, rows=5, cols=19):
    sub_ims = []
    with Image.open(filename) as im:
        for i in range(cols*rows):
            coords = (grid_width*(i%cols), grid_height*(floor(i/cols)), grid_width*(i%cols + 1), grid_height * (1 + floor(i/cols)))
            sub_im = im.crop(coords)
            sub_ims.append(sub_im)
            sub_im.save(f"{i}.png")
    return sub_ims

def parsePixels(sub_ims, grid_width, grid_height):
    px_colors = []
    for s in sub_ims:
        char_px = []
        px = s.load()
        for i in range(grid_height):
            for j in range(grid_width):
                color = px[j,i]
                color = (color[0] >> 3, color[1] >> 2, color[2] >> 3)
                color = (color[0] << 11, color[1] << 5, color[2])
                color = hex(color[0] + color[1] + color[2])
                char_px.append(color)
        px_colors.append(char_px)
    return px_colors

def genTestString(char_width):
    

if __name__ == '__main__':
    grid_width = 13
    grid_height = 24

    sub_ims = genSubImages("Inconsolata_13_24.png", grid_width, grid_height)
    pixel_tupels = parsePixels(sub_ims, grid_width, grid_height)
    
    with open('font.hh', 'w+') as f:
        f.write('#include "pico/stdlib.h"\n\n')
        f.write(f"struct Font {{\n")
        f.write(f"    char letter;\n")
        f.write(f"    u_int16_t code[{grid_width}*{grid_height}];\n")
        f.write(f"}};\n")
        f.write(f"struct Font font[] = {{\n")

        for idx, pixels in enumerate(pixel_tupels):
            f.write(f"{{'")
            if characters[idx] in ["'", "\\"]:
                f.write(f"\\{characters[idx]}")
            else:
                f.write(f"{characters[idx]}")
            f.write(f"', {{")
                    
            for i, p in enumerate(pixels):
                if i % grid_width == 0:
                    f.write("\n")
                if len(p) != 6:
                    p = f"0x{''.join(['0' for j in range(6-len(p))])}{p[2:]}"
                f.write(f"{p}")
                if (i != grid_width*grid_height-1):
                    f.write(",")
            f.write(f"}}}}")
            if idx != len(pixel_tupels)-1:
                f.write(f",\n")
            else:
                f.write(f"}};")
    
    