from PIL import Image
from math import floor

characters = ' !"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~'
characters = [c for c in characters]

def genSubImages(filename):
    sub_ims = []
    with Image.open(filename) as im:
        for i in range(19):
            coords = (24*(i%19), 24*(floor(i/19)), 24*(i%19 + 1), 24 * (1 + floor(i/19)))
            sub_im = im.crop(coords)
            sub_ims.append(sub_im)
    return sub_ims

def parsePixels(sub_ims):
    px_colors = []
    for s in sub_ims:
        char_px = []
        px = s.load()
        for i in range(24):
            for j in range(24):
                color = px[j,i]
                color = (color[0] >> 3, color[1] >> 2, color[2] >> 3)
                color = (color[0] << 11, color[1] << 5, color[2])
                color = hex(color[0] + color[1] + color[2])
                char_px.append(color)
        px_colors.append(char_px)
    return px_colors



if __name__ == '__main__':
    sub_ims = genSubImages("Inconsolata.png")
    pixel_tupels = parsePixels(sub_ims)
    with open('output.hh', 'w+') as f:
        f.write("struct Font {\n")
        f.write("    char letter;\n")
        f.write("    char code[24*24];\n")
        f.write("};\n")
        f.write("struct Font font[] = {\n")

        for idx, pixels in enumerate(pixel_tupels):
            f.write(f"{{'")
            if characters[idx] == "'":
                f.write(f"\\{characters[idx]}")
            else:
                f.write(f"{characters[idx]}")
            f.write(f"', {{")
                    
            for i, p in enumerate(pixels):
                if i % 24 == 0:
                    f.write("\n")
                if len(p) != 6:
                    p = f"0x{''.join(['0' for j in range(6-len(p))])}{p[2:]}"
                f.write(f"{p}")
                if (i != 24*24-1):
                    f.write(",")
            f.write(f"}}}}")
            if idx != len(pixel_tupels)-1:
                f.write(f",\n")
            else:
                f.write(f"}};")