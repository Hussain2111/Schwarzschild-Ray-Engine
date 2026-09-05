// Framebuffer plus a self-contained PNG writer.
//
// The PNG encoder is ~80 lines and pulls in nothing: it emits a zlib stream
// made of *stored* (uncompressed) deflate blocks, which is a legal deflate
// stream that every decoder accepts. Files are larger than a compressed PNG,
// but the engine keeps its "clone and build, no package manager" property.
// If you would rather have small files, link zlib and swap out deflateRaw().
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "sre/color.hpp"

namespace sre {

class Image {
public:
    Image(int w, int h) : width_(w), height_(h), pixels_(static_cast<size_t>(w) * h) {}

    int width() const { return width_; }
    int height() const { return height_; }

    Color& at(int x, int y) { return pixels_[static_cast<size_t>(y) * width_ + x]; }
    const Color& at(int x, int y) const { return pixels_[static_cast<size_t>(y) * width_ + x]; }

    /// Add a bloom: bright regions bleed into their surroundings.
    ///
    /// Not decoration. The disk spans several orders of magnitude of radiance
    /// against a black sky, and every real optical system -- eye, camera,
    /// telescope -- spreads that energy. Without it the tone-mapped disk reads
    /// as flat paint with a hard edge; with it the brightness ordering survives
    /// the trip through an 8-bit display.
    void applyBloom(double threshold, double strength, int radius) {
        if (strength <= 0.0 || radius < 1) return;

        const size_t n = pixels_.size();
        std::vector<Color> bright(n);
        for (size_t i = 0; i < n; ++i) {
            const double y = color::luminance(pixels_[i]);
            const double excess = y - threshold;
            bright[i] = excess > 0.0 ? pixels_[i] * (excess / std::max(y, 1e-9)) : Color{0, 0, 0};
        }

        // Separable Gaussian, so cost is O(radius) per pixel rather than O(radius^2).
        std::vector<double> kernel(radius + 1);
        const double sigma = radius / 2.5;
        double norm = 0.0;
        for (int i = 0; i <= radius; ++i) {
            kernel[i] = std::exp(-0.5 * (i * i) / (sigma * sigma));
            norm += (i == 0 ? 1.0 : 2.0) * kernel[i];
        }
        for (auto& k : kernel) k /= norm;

        std::vector<Color> tmp(n);
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) {
                Color acc = bright[idx(x, y)] * kernel[0];
                for (int i = 1; i <= radius; ++i) {
                    acc += bright[idx(std::clamp(x - i, 0, width_ - 1), y)] * kernel[i];
                    acc += bright[idx(std::clamp(x + i, 0, width_ - 1), y)] * kernel[i];
                }
                tmp[idx(x, y)] = acc;
            }
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) {
                Color acc = tmp[idx(x, y)] * kernel[0];
                for (int i = 1; i <= radius; ++i) {
                    acc += tmp[idx(x, std::clamp(y - i, 0, height_ - 1))] * kernel[i];
                    acc += tmp[idx(x, std::clamp(y + i, 0, height_ - 1))] * kernel[i];
                }
                pixels_[idx(x, y)] += acc * strength;
            }
    }

    /// Apply exposure, tone map, and encode to 8-bit sRGB.
    std::vector<uint8_t> toSRGB8(double exposure) const {
        std::vector<uint8_t> out(pixels_.size() * 3);
        for (size_t i = 0; i < pixels_.size(); ++i) {
            const Color mapped = color::toneMap(pixels_[i] * exposure);
            const Color srgb = color::linearToSRGB(mapped);
            out[i * 3 + 0] = static_cast<uint8_t>(srgb.x * 255.0 + 0.5);
            out[i * 3 + 1] = static_cast<uint8_t>(srgb.y * 255.0 + 0.5);
            out[i * 3 + 2] = static_cast<uint8_t>(srgb.z * 255.0 + 0.5);
        }
        return out;
    }

    bool writePNG(const std::string& path, double exposure) const;
    bool writePPM(const std::string& path, double exposure) const;

private:
    size_t idx(int x, int y) const { return static_cast<size_t>(y) * width_ + x; }

    int width_, height_;
    std::vector<Color> pixels_;
};

namespace png {

inline uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

inline uint32_t adler32(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

inline void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

inline void chunk(std::vector<uint8_t>& out, const char tag[4], const std::vector<uint8_t>& data) {
    put32(out, static_cast<uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uint32_t c = crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu;
    put32(out, c);
}

/// zlib stream built from stored deflate blocks. Correct, if not compact.
inline std::vector<uint8_t> deflateStored(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> z;
    z.push_back(0x78);  // CMF: deflate, 32K window
    z.push_back(0x01);  // FLG: no dictionary, check bits make 0x7801 % 31 == 0

    constexpr size_t kBlock = 65535;
    size_t offset = 0;
    do {
        const size_t n = std::min(kBlock, raw.size() - offset);
        const bool last = (offset + n) >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xFF));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + offset, raw.begin() + offset + n);
        offset += n;
    } while (offset < raw.size());

    const uint32_t ad = adler32(raw.data(), raw.size());
    put32(z, ad);
    return z;
}

/// Encode 8-bit RGB to a PNG byte stream.
inline std::vector<uint8_t> encodeRGB8(const uint8_t* rgb, int width, int height) {
    // Scanlines with a leading filter byte. Filter 1 (Sub) predicts each byte
    // from the pixel to its left, which knocks a lot of redundancy out of a
    // smooth image even though we then store it uncompressed.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + 3 * width));
    for (int y = 0; y < height; ++y) {
        raw.push_back(1);
        const uint8_t* row = rgb + static_cast<size_t>(y) * width * 3;
        for (int x = 0; x < width * 3; ++x) {
            const uint8_t left = x >= 3 ? row[x - 3] : 0;
            raw.push_back(static_cast<uint8_t>(row[x] - left));
        }
    }

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<uint8_t> ihdr;
    put32(ihdr, static_cast<uint32_t>(width));
    put32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // colour type: truecolour RGB
    ihdr.push_back(0);  // deflate
    ihdr.push_back(0);  // adaptive filtering
    ihdr.push_back(0);  // no interlace
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", deflateStored(raw));
    chunk(out, "IEND", {});
    return out;
}

}  // namespace png

inline bool Image::writePNG(const std::string& path, double exposure) const {
    const std::vector<uint8_t> rgb = toSRGB8(exposure);
    const std::vector<uint8_t> data = png::encodeRGB8(rgb.data(), width_, height_);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

inline bool Image::writePPM(const std::string& path, double exposure) const {
    const std::vector<uint8_t> rgb = toSRGB8(exposure);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", width_, height_);
    const bool ok = std::fwrite(rgb.data(), 1, rgb.size(), f) == rgb.size();
    std::fclose(f);
    return ok;
}

}  // namespace sre
