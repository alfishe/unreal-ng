"""Color mixers (design-mixers.md). A mixer turns weighted palette colors into one color."""
import numpy as np


def srgb_to_linear(c):
    c = c / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055) * 255.0


class LinearMean:
    """Average in linear light (default): the eye integrates emitted light."""
    name = "linear-mean"

    def __init__(self, palette_rgb):
        self.lin = srgb_to_linear(palette_rgb.astype(np.float64))   # 256 x 3

    def mix(self, planes, weights):
        """planes: K x H x W palette indices, weights: K x H x W (sum to 1 per pixel) -> H x W x 3 uint8"""
        acc = np.zeros(planes.shape[1:] + (3,))
        for k in range(planes.shape[0]):
            acc += self.lin[planes[k]] * weights[k][..., None]
        return np.round(linear_to_srgb(acc)).astype(np.uint8)


class SrgbMean(LinearMean):
    """Plain average of stored values (what FrameHistory does today) - for comparison."""
    name = "srgb-mean"

    def __init__(self, palette_rgb):
        self.lin = palette_rgb.astype(np.float64)

    def mix(self, planes, weights):
        acc = np.zeros(planes.shape[1:] + (3,))
        for k in range(planes.shape[0]):
            acc += self.lin[planes[k]] * weights[k][..., None]
        return np.round(np.clip(acc, 0, 255)).astype(np.uint8)


MIXERS = {m.name: m for m in (LinearMean, SrgbMean)}
