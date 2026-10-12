/* ================================================================================================
 * File: imgdump.cpp
 * Created on: 26/10/15
 * Brief: Dumps PCX images into C++ byte arrays, to be compiled into the ELF. The built-ins under
 *        src/ps2/builtin/ come from it: the console font and background, the HUD backtile, the id
 *        logo the boot splash shows, and the global palette. They are there before any game data
 *        has been found.
 *
 *          imgdump [--format pal|rgba32|rgb16] [--palette <colormap.pcx>] [--out <dir>] <image.pcx>...
 *
 *            --format F   pal (the default) keeps the 8-bit palette indices as they are: the PS2
 *                         uploads them as PSMT8 textures sampling the global palette's CLUT.
 *                         rgba32 and rgb16 expand them through a palette, into RGBA 8:8:8:8 or
 *                         RGB 5:5:5:1 texels.
 *            --palette P  a PCX whose palette is the global one (pics/colormap.pcx). It is written
 *                         to palette.cpp as global_palette, and rgba32/rgb16 expand through it.
 *                         Without it they use each image's own palette.
 *            --out D      where the .cpp files go (default: the current directory).
 *
 *        Each <name>.pcx becomes <name>.cpp, defining <name>_width, <name>_height,
 *        <name>_size_bytes and <name>_data, which builtin.h declares. Images are decoded the way
 *        the game's own loader decodes them (renderer/image_load.cpp), so a built-in holds the
 *        same pixels as the file it stands in for. To regenerate them all from an unpacked pak0:
 *
 *          imgdump --palette pics/colormap.pcx --out src/ps2/builtin pics/conchars.pcx \
 *                  pics/conback.pcx pics/backtile.pcx pics/m_main_logo.pcx
 *
 * This source code is released under the GNU GPL v2 license.
 * Check the accompanying LICENSE file for details.
 * ================================================================================================ */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// ------------------------------------------------------------------------------------------------
// PCX
// ------------------------------------------------------------------------------------------------

constexpr int          kPcxHeaderBytes   = 128;
constexpr int          kPaletteEntries   = 256;
constexpr int          kPcxPaletteBytes  = kPaletteEntries * 3;
constexpr std::uint8_t kPcxPaletteMarker = 0x0C; // the byte ahead of a 256-colour palette
constexpr int          kTransparentIndex = 255;  // Quake's see-through palette entry

enum class Format
{
    Pal,
    Rgba32,
    Rgb16,
};

struct Image
{
    int width  = 0;
    int height = 0;
    std::vector<std::uint8_t> indices; // width * height palette indices, row by row
    std::uint32_t palette[kPaletteEntries] = {}; // the file's own palette, 0xAABBGGRR
};

bool Fail(const char * format, const char * arg = "")
{
    std::fprintf(stderr, "imgdump: error: ");
    std::fprintf(stderr, format, arg);
    std::fprintf(stderr, "\n");
    return false;
}

bool ReadWholeFile(const char * path, std::vector<std::uint8_t> & out)
{
    std::FILE * file = std::fopen(path, "rb");
    if (file == nullptr)
    {
        return Fail("cannot open '%s'", path);
    }

    bool ok = (std::fseek(file, 0, SEEK_END) == 0);
    const long size = ok ? std::ftell(file) : -1;
    ok = ok && size >= 0 && std::fseek(file, 0, SEEK_SET) == 0;
    if (ok)
    {
        out.resize(static_cast<size_t>(size));
        ok = (size == 0) || std::fread(out.data(), 1, out.size(), file) == out.size();
    }
    std::fclose(file);
    return ok ? true : Fail("cannot read '%s'", path);
}

int ReadU16(const std::uint8_t * bytes)
{
    return bytes[0] | (bytes[1] << 8);
}

// Reads a PCX's header and palette, and its pixels too when 'pixels' is set. The same checks and
// the same decoding as the game's LoadPcx: width and height from xmax/ymax, and RLE runs that
// overshoot a row (its padding bytes) consumed without being stored.
bool LoadPcx(const char * path, const bool pixels, Image & image)
{
    std::vector<std::uint8_t> file;
    if (!ReadWholeFile(path, file))
    {
        return false;
    }
    if (file.size() < static_cast<size_t>(kPcxHeaderBytes + 1 + kPcxPaletteBytes))
    {
        return Fail("'%s' is too short to be a PCX", path);
    }

    // manufacturer, version, encoding, bits per pixel, then xmin, ymin, xmax and ymax.
    const std::uint8_t * header = file.data();
    const int xmax = ReadU16(header + 8);
    const int ymax = ReadU16(header + 10);
    if (header[0] != 0x0A || header[1] != 5 || header[2] != 1 || header[3] != 8 || xmax >= 640 || ymax >= 480)
    {
        return Fail("'%s' is not an 8-bit PCX the game can load", path);
    }

    const std::uint8_t * palette = file.data() + file.size() - kPcxPaletteBytes;
    if (palette[-1] != kPcxPaletteMarker)
    {
        return Fail("'%s' has no 256-colour palette", path);
    }
    for (int i = 0; i < kPaletteEntries; ++i)
    {
        const std::uint32_t r = palette[(i * 3) + 0];
        const std::uint32_t g = palette[(i * 3) + 1];
        const std::uint32_t b = palette[(i * 3) + 2];
        image.palette[i] = 0xFF000000u | (b << 16) | (g << 8) | r;
    }
    image.palette[kTransparentIndex] &= 0x00FFFFFFu;

    image.width  = xmax + 1;
    image.height = ymax + 1;
    if (!pixels)
    {
        return true;
    }

    image.indices.assign(static_cast<size_t>(image.width * image.height), 0);
    const std::uint8_t * in  = file.data() + kPcxHeaderBytes;
    const std::uint8_t * end = file.data() + file.size();
    for (int y = 0; y < image.height; ++y)
    {
        std::uint8_t * row = &image.indices[static_cast<size_t>(y * image.width)];
        for (int x = 0; x < image.width;)
        {
            if (in >= end)
            {
                return Fail("'%s' is malformed: its pixels end early", path);
            }

            int dataByte  = *in++;
            int runLength = 1;
            if ((dataByte & 0xC0) == 0xC0)
            {
                runLength = dataByte & 0x3F;
                if (in >= end)
                {
                    return Fail("'%s' is malformed: its pixels end early", path);
                }
                dataByte = *in++;
            }

            while (runLength-- > 0 && x < image.width)
            {
                row[x++] = static_cast<std::uint8_t>(dataByte);
            }
        }
    }
    return true;
}

// ------------------------------------------------------------------------------------------------
// Expansion through a palette
// ------------------------------------------------------------------------------------------------

// RGBA 8:8:8:8 as the palette's own bytes. A transparent texel takes the colour of an opaque
// neighbour, so bilinear filtering has no dark fringe to blend in at the edge of a cut-out.
std::vector<std::uint8_t> ExpandRgba32(const Image & image, const std::uint32_t (&palette)[kPaletteEntries])
{
    const int numTexels = image.width * image.height;
    const int w = image.width;
    const std::uint8_t * in = image.indices.data();
    std::vector<std::uint8_t> out(static_cast<size_t>(numTexels) * 4);

    for (int i = 0; i < numTexels; ++i)
    {
        std::uint32_t texel = palette[in[i]];

        if (in[i] == kTransparentIndex)
        {
            int neighbour = 0;
            if (i > w && in[i - w] != kTransparentIndex)
            {
                neighbour = in[i - w];
            }
            else if (i < numTexels - w && in[i + w] != kTransparentIndex)
            {
                neighbour = in[i + w];
            }
            else if (i > 0 && in[i - 1] != kTransparentIndex)
            {
                neighbour = in[i - 1];
            }
            else if (i < numTexels - 1 && in[i + 1] != kTransparentIndex)
            {
                neighbour = in[i + 1];
            }
            texel = (texel & 0xFF000000u) | (palette[neighbour] & 0x00FFFFFFu);
        }

        std::memcpy(&out[static_cast<size_t>(i) * 4], &texel, sizeof(texel));
    }
    return out;
}

// RGB 5:5:5 plus the top bit of the palette alpha, the GS's PSMCT16 layout, little-endian.
std::vector<std::uint8_t> ExpandRgb16(const Image & image, const std::uint32_t (&palette)[kPaletteEntries])
{
    const int numTexels = image.width * image.height;
    std::vector<std::uint8_t> out(static_cast<size_t>(numTexels) * 2);

    for (int i = 0; i < numTexels; ++i)
    {
        const std::uint32_t colour = palette[image.indices[static_cast<size_t>(i)]];
        const std::uint32_t r = colour & 0xFFu;
        const std::uint32_t g = (colour >> 8) & 0xFFu;
        const std::uint32_t b = (colour >> 16) & 0xFFu;
        const std::uint32_t a = (colour >> 24) & 0xFFu;

        const std::uint32_t texel = ((a & 0x1u) << 15) | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
        out[static_cast<size_t>(i) * 2 + 0] = static_cast<std::uint8_t>(texel & 0xFFu);
        out[static_cast<size_t>(i) * 2 + 1] = static_cast<std::uint8_t>(texel >> 8);
    }
    return out;
}

// ------------------------------------------------------------------------------------------------
// Output
// ------------------------------------------------------------------------------------------------

bool CloseOutput(std::FILE * file, const std::string & path)
{
    const bool written = (std::ferror(file) == 0);
    const bool closed  = (std::fclose(file) == 0);
    return (written && closed) ? true : Fail("cannot write '%s'", path.c_str());
}

bool WritePalette(const std::string & path, const std::uint32_t (&palette)[kPaletteEntries])
{
    std::FILE * file = std::fopen(path.c_str(), "wt");
    if (file == nullptr)
    {
        return Fail("cannot create '%s'", path.c_str());
    }

    std::fprintf(file, "// File generated by imgdump\n#include \"builtin.h\"\n\n");
    std::fprintf(file, "alignas(16) const unsigned int global_palette[256] = {\n    ");
    int column = 0;
    for (int i = 0; i < kPaletteEntries; ++i)
    {
        std::fprintf(file, "0x%08X", palette[i]);
        if (i != kPaletteEntries - 1)
        {
            std::fprintf(file, ", ");
        }
        if (++column > 4)
        {
            std::fprintf(file, "\n    ");
            column = 0;
        }
    }
    std::fprintf(file, "\n};\n\n");
    return CloseOutput(file, path);
}

bool WriteImage(const std::string & path, const std::string & name, const char * contents,
                const Image & image, const std::vector<std::uint8_t> & bytes)
{
    std::FILE * file = std::fopen(path.c_str(), "wt");
    if (file == nullptr)
    {
        return Fail("cannot create '%s'", path.c_str());
    }

    const int numBytes = static_cast<int>(bytes.size());
    std::fprintf(file, "// File generated by imgdump (%s)\n#include \"builtin.h\"\n\n", contents);
    std::fprintf(file, "const int %s_width  = %d;\n", name.c_str(), image.width);
    std::fprintf(file, "const int %s_height = %d;\n", name.c_str(), image.height);
    std::fprintf(file, "const int %s_size_bytes = %d;\n", name.c_str(), numBytes);
    std::fprintf(file, "alignas(16) const unsigned char %s_data[] = {\n    ", name.c_str());
    int column = 0;
    for (int i = 0; i < numBytes; ++i)
    {
        std::fprintf(file, "0x%02X", bytes[static_cast<size_t>(i)]);
        if (i != numBytes - 1)
        {
            std::fprintf(file, ", ");
        }
        if (++column > 14)
        {
            std::fprintf(file, "\n    ");
            column = 0;
        }
    }
    std::fprintf(file, "\n};\n\n");
    return CloseOutput(file, path);
}

// The file name without its folders or extension, made a C identifier: "pics/m_main_logo.pcx"
// names the m_main_logo_* symbols, in m_main_logo.cpp.
std::string SymbolName(const char * path)
{
    const char * base = path;
    for (const char * p = path; *p != '\0'; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            base = p + 1;
        }
    }

    std::string name(base);
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot != 0)
    {
        name.resize(dot);
    }

    for (char & c : name)
    {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        c = valid ? c : '_';
    }
    if (name.empty() || (name[0] >= '0' && name[0] <= '9'))
    {
        name.insert(0, 1, '_');
    }
    return name;
}

// ------------------------------------------------------------------------------------------------
// Command line
// ------------------------------------------------------------------------------------------------

struct Options
{
    Format                     format      = Format::Pal;
    const char *               palettePath = nullptr;
    std::string                outDir      = ".";
    std::vector<const char *>  images;
};

void PrintUsage()
{
    std::fprintf(stderr,
                 "usage: imgdump [--format pal|rgba32|rgb16] [--palette <colormap.pcx>] [--out <dir>] <image.pcx>...\n"
                 "  --format F   pal (default): 8-bit palette indices; rgba32/rgb16: expanded through a palette\n"
                 "  --palette P  write P's palette to palette.cpp, and expand rgba32/rgb16 through it\n"
                 "               (without it, each image's own palette)\n"
                 "  --out D      directory for the .cpp files (default: the current one)\n"
                 "Each <name>.pcx becomes <name>.cpp, defining <name>_width/_height/_size_bytes/_data.\n");
}

bool ParseArgs(const int argc, char ** argv, Options & opts)
{
    for (int i = 1; i < argc; ++i)
    {
        const char * arg = argv[i];
        const bool hasValue = (i + 1 < argc);

        if (std::strcmp(arg, "--format") == 0 && hasValue)
        {
            const char * format = argv[++i];
            if (std::strcmp(format, "pal") == 0)
            {
                opts.format = Format::Pal;
            }
            else if (std::strcmp(format, "rgba32") == 0)
            {
                opts.format = Format::Rgba32;
            }
            else if (std::strcmp(format, "rgb16") == 0)
            {
                opts.format = Format::Rgb16;
            }
            else
            {
                return Fail("--format takes pal, rgba32 or rgb16, not '%s'", format);
            }
        }
        else if (std::strcmp(arg, "--palette") == 0 && hasValue)
        {
            opts.palettePath = argv[++i];
        }
        else if (std::strcmp(arg, "--out") == 0 && hasValue)
        {
            opts.outDir = argv[++i];
        }
        else if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0)
        {
            return false;
        }
        else if (arg[0] == '-' && arg[1] != '\0')
        {
            return Fail("unknown or incomplete option '%s'", arg);
        }
        else
        {
            opts.images.push_back(arg);
        }
    }

    if (opts.images.empty() && opts.palettePath == nullptr)
    {
        return Fail("nothing to dump: give one or more images, or --palette");
    }
    return true;
}

std::string OutputPath(const Options & opts, const std::string & fileName)
{
    if (opts.outDir.empty() || opts.outDir.back() == '/')
    {
        return opts.outDir + fileName;
    }
    return opts.outDir + "/" + fileName;
}

} // namespace

int main(int argc, char ** argv)
{
    Options opts;
    if (!ParseArgs(argc, argv, opts))
    {
        PrintUsage();
        return 1;
    }

    // Two inputs with one name would write one file over the other.
    std::vector<std::string> names;
    for (const char * imagePath : opts.images)
    {
        const std::string name = SymbolName(imagePath);
        for (const std::string & seen : names)
        {
            if (seen == name)
            {
                Fail("two images would both be dumped as '%s'", name.c_str());
                return 1;
            }
        }
        if (opts.palettePath != nullptr && name == "palette")
        {
            Fail("an image named 'palette' would overwrite palette.cpp");
            return 1;
        }
        names.push_back(name);
    }

    Image paletteSource;
    if (opts.palettePath != nullptr)
    {
        const std::string outPath = OutputPath(opts, "palette.cpp");
        if (!LoadPcx(opts.palettePath, /*pixels=*/false, paletteSource) || !WritePalette(outPath, paletteSource.palette))
        {
            return 1;
        }
        std::printf("%s -> %s (global_palette)\n", opts.palettePath, outPath.c_str());
    }

    for (size_t i = 0; i < opts.images.size(); ++i)
    {
        const char * imagePath = opts.images[i];
        Image image;
        if (!LoadPcx(imagePath, /*pixels=*/true, image))
        {
            return 1;
        }

        const std::uint32_t (&palette)[kPaletteEntries] = (opts.palettePath != nullptr) ? paletteSource.palette : image.palette;
        const char * paletteName = (opts.palettePath != nullptr) ? "the global palette" : "the image's own palette";

        std::vector<std::uint8_t> bytes;
        std::string contents;
        switch (opts.format)
        {
        case Format::Pal:
            bytes    = image.indices;
            contents = "8-bit palette indices into global_palette";
            break;
        case Format::Rgba32:
            bytes    = ExpandRgba32(image, palette);
            contents = std::string("RGBA 8:8:8:8 through ") + paletteName;
            break;
        case Format::Rgb16:
            bytes    = ExpandRgb16(image, palette);
            contents = std::string("RGB 5:5:5:1 through ") + paletteName;
            break;
        }

        const std::string outPath = OutputPath(opts, names[i] + ".cpp");
        if (!WriteImage(outPath, names[i], contents.c_str(), image, bytes))
        {
            return 1;
        }
        std::printf("%s -> %s (%dx%d, %d bytes)\n", imagePath, outPath.c_str(), image.width, image.height,
                    static_cast<int>(bytes.size()));
    }
    return 0;
}
