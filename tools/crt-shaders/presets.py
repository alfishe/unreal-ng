"""The CRT presets of unreal-qt and the gallery tools: the libretro presets (data/shaders/crt/<key>/, `shader:<key>`),
unreal-qt's own profiles (CRTRenderer, CRTBeamModel: `crt-render`) and the gallery's descriptions."""

PRESETS = [  # (key, preset relative to the slang-shaders checkout)
    ('crt-lottes', 'crt/crt-lottes.slangp'),
    ('crt-lottes-glow', 'crt/crt-lottes-multipass-glow.slangp'),
    ('crt-geom', 'crt/crt-geom.slangp'),
    ('crt-easymode', 'crt/crt-easymode.slangp'),
    ('crt-hyllian', 'crt/crt-hyllian.slangp'),
    ('crt-gdv-mini', 'crt/crt-gdv-mini.slangp'),
    ('guest-advanced', 'crt/crt-guest-advanced.slangp'),
    ('guest-advanced-fast', 'crt/crt-guest-advanced-fast.slangp'),
    ('guest-pal', 'crt/crt-guest-advanced-pal.slangp'),
    ('guest-pal-composite', 'pal/shaders/decoupled-guest/decoupled-guest-advanced-pal 2-Composite.slangp'),
    ('crt-beans-rgb', 'crt/crt-beans-rgb.slangp'),
    ('megatron-v2-sdr', 'hdr/crt-sony-megatron-v2-default-sdr.slangp'),
    ('crt-royale', 'crt/crt-royale.slangp'),
    ('crt-yah', 'crt/crt-yah.slangp'),
    ('crt-yo6-pvm', 'crt/crt-yo6-KV-M1420B.slangp'),
    ('crt-consumer', 'crt/crt-consumer.slangp'),
    ('crt-pi', 'crt/crt-pi.slangp'),
    ('zfast-crt', 'crt/zfast-crt.slangp'),
    ('newpixie', 'crt/newpixie-crt.slangp'),
]

# unreal-qt's profiles (their automation words): the plain picture, Basic, the integer models of the single-pass shaders
MODELS = ['crt-lottes', 'crt-geom', 'crt-easymode', 'crt-gdv-mini', 'crt-consumer', 'crt-pi', 'zfast-crt']
OURS = ['none', 'basic'] + MODELS

# The gallery's size: 4x the 352 x 288 frame
OW, OH = 1408, 1152

INFO = {  # gallery key: (title, license, passes, note); ours as ours-<profile key>
    'ours-none': ('Ours: None (nearest)', 'GPL-3', 1, 'the plain picture, the reference'),
    'ours-basic': ('Ours: Basic', 'GPL-3', 1, 'CPU = GPU bit for bit'),
    **{f'ours-{k}': (f'Ours: {k} model', 'GPL-3', 1, f'integer model of {k}, CPU = GPU bit for bit') for k in MODELS},
    'crt-lottes': ('crt-lottes', 'public domain', 1, 'gaussian scanlines + shadow mask, linear light'),
    'crt-lottes-glow': ('crt-lottes multipass glow', 'public domain', 7, 'lottes + separable glow'),
    'crt-geom': ('crt-geom', 'GPL-2+', 1, 'curvature, Lanczos, beam width by brightness'),
    'crt-easymode': ('crt-easymode', 'GPL', 1, 'sharp, simple masks'),
    'crt-hyllian': ('crt-hyllian', 'license: check', 5, 'sharp horizontal filter, PVM look'),
    'crt-gdv-mini': ('guest gdv-mini', 'GPL-2+', 1, 'guest-advanced in one pass'),
    'guest-advanced': ('crt-guest-advanced', 'GPL-2+', 12, 'the feature-rich reference'),
    'guest-advanced-fast': ('crt-guest-advanced fast', 'GPL-2+', 9, ''),
    'guest-pal': ('crt-guest-advanced PAL', 'GPL-2+', 19, 'PAL signal model + guest CRT'),
    'guest-pal-composite': ('guest PAL composite (decoupled)', 'GPL-2+', 10, 'PAL composite artefacts + guest CRT'),
    'crt-beans-rgb': ('crt-beans RGB', 'MIT', 12, 'analog bandwidth, resolution-independent scanlines, glow'),
    'megatron-v2-sdr': ('Sony Megatron v2 (SDR)', 'license: check', 2,
                        'subpixel mask: made for HDR ~600+ nit, ~3x darker in SDR'),
    'crt-royale': ('crt-royale', 'GPL-2+', 12, 'the accuracy reference, heavy'),
    'crt-yah': ('crt-yah', 'license: check', 13, 'newer all-in-one'),
    'crt-yo6-pvm': ('crt-yo6 KV-M1420B', 'license: check', 3, 'a model of one Sony TV'),
    'crt-consumer': ('crt-consumer', 'GPL-2+', 1, 'consumer TV look'),
    'crt-pi': ('crt-pi', 'GPL-2+', 1, 'for Raspberry Pi'),
    'zfast-crt': ('zfast-crt', 'GPL-2+', 1, 'the cheapest'),
    'newpixie': ('newpixie-crt', 'public domain', 4, 'blur, ghosting, stylised'),
}
ORDER = list(INFO)
