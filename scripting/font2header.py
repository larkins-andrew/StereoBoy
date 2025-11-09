from PIL import Image
from math import floor

characters = ' !"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~'
characters = [c for c in characters]

def genSubImages(filename):
    sub_ims = []
    with Image.open(filename) as im:
        for i in range(2):
            coords = (24*(i%19), 24*(floor(i/19)), 24*(i%19 + 1), 24 * (1 + floor(i/19)))
            print(coords)
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
                # print(px[i,j])
                color = px[i,j]
                color = (color[0] >> 3, color[1] >> 2, color[2] >> 3)
                color = (color[0] << 11, color[1] << 5, color[2])
                # print((hex(color[0] << 11), hex(color[1] << 5), hex(color[2])))
                color = hex(color[0] + color[1] + color[2])
                char_px.append(color)
                # print(color)
        px_colors.append(char_px)
    return px_colors



if __name__ == '__main__':
    sub_ims = genSubImages("Inconsolata.png")
    pixel_tupels = parsePixels(sub_ims)
    print(pixel_tupels[1])
    with open('output.txt', 'w+') as f:
        for s in pixel_tupels[1]:
            f.write(f"{s}, ")