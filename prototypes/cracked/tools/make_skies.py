# Makes the seven maps' skies for CRACKED from the Unity project's six-sided
# skyboxes: each face scaled to SIZE pixels and saved as a JPEG named for its
# cube face, as Tide's `asset Cubemap` wants them. The pack holds a face's
# pixels, not its JPEG: 256 a side is 11 MB for the seven skies, where 512
# was 44.
#   blender -b --python make_skies.py -- <Unity Assets folder> <art/sky folder>
import bpy, sys, os
SIZE = 256
src, dst = sys.argv[sys.argv.index("--")+1:][:2]
A = os.path.join(src, "Externals", "Allsky")
skies = [
    (A + "/Cartoon/Cartoon Base BlueSky/Sky_Day_BlueSky_Nothing_Cam_"),
    (A + "/Anime/Anime Day/Anime Day_Cam_"),
    (A + "/Sunless_Moonless/Sunless_BlueSky_03/Sunless_BlueSky_03_Cam_"),
    (A + "/Cartoon/Cartoon Oils Sunset Wild/Sky_CartoonSunset2_Cam_"),
    (A + "/Space/Space Planet B/Space Planet B_Cam_"),
    (A + "/Space/Space_InTheVortex/Space_InTheVortex_Cam_"),
    (A + "/Mammatus/Mammatus Sunset A/Mammatus Sunset A_Cam_"),
]
faces = [("0_Front+Z", "pz"), ("1_Back-Z", "nz"), ("2_Left+X", "px"), ("3_Right-X", "nx"), ("4_Up+Y", "py"), ("5_Down-Y", "ny")]
os.makedirs(dst, exist_ok=True)
scene = bpy.context.scene
scene.render.image_settings.file_format = 'JPEG'
scene.render.image_settings.quality = 88
scene.render.image_settings.color_mode = 'RGB'
scene.view_settings.view_transform = 'Standard'
for i, prefix in enumerate(skies):
    for unity, tide in faces:
        path = prefix + unity + ".png"
        image = bpy.data.images.load(path)
        print("SKY", i, tide, image.size[:], os.path.basename(path))
        # Halved a step at a time: one big step skips pixels, and the sky comes out blocky
        side = image.size[0]
        while side > SIZE:
            side = max(side // 2, SIZE)
            image.scale(side, side)
        out = os.path.join(dst, "sky%d_%s.jpg" % (i, tide))
        image.save_render(out, scene=scene)
        bpy.data.images.remove(image)
