#include "text_renderer.h"

#ifdef __EMSCRIPTEN__
#include <array>
#include <cctype>
#include <vector>

namespace {
using GlyphRows = std::array<const char*, 7>;

GlyphRows glyphRows(char c)
{
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (c) {
    case 'A': return { " ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #" };
    case 'B': return { "#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### " };
    case 'C': return { " ####", "#    ", "#    ", "#    ", "#    ", "#    ", " ####" };
    case 'D': return { "#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### " };
    case 'E': return { "#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####" };
    case 'F': return { "#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    " };
    case 'G': return { " ####", "#    ", "#    ", "#  ##", "#   #", "#   #", " ####" };
    case 'H': return { "#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #" };
    case 'I': return { "#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####" };
    case 'J': return { "#####", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  " };
    case 'K': return { "#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #" };
    case 'L': return { "#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####" };
    case 'M': return { "#   #", "## ##", "# # #", "#   #", "#   #", "#   #", "#   #" };
    case 'N': return { "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #" };
    case 'O': return { " ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### " };
    case 'P': return { "#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    " };
    case 'Q': return { " ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #" };
    case 'R': return { "#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #" };
    case 'S': return { " ####", "#    ", "#    ", " ### ", "    #", "    #", "#### " };
    case 'T': return { "#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  " };
    case 'U': return { "#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### " };
    case 'V': return { "#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  " };
    case 'W': return { "#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #" };
    case 'X': return { "#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #" };
    case 'Y': return { "#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  " };
    case 'Z': return { "#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####" };
    case '0': return { " ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### " };
    case '1': return { "  #  ", " ##  ", "# #  ", "  #  ", "  #  ", "  #  ", "#####" };
    case '2': return { " ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####" };
    case '3': return { "#### ", "    #", "    #", " ### ", "    #", "    #", "#### " };
    case '4': return { "#   #", "#   #", "#   #", "#####", "    #", "    #", "    #" };
    case '5': return { "#####", "#    ", "#    ", "#### ", "    #", "    #", "#### " };
    case '6': return { " ### ", "#    ", "#    ", "#### ", "#   #", "#   #", " ### " };
    case '7': return { "#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   " };
    case '8': return { " ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### " };
    case '9': return { " ### ", "#   #", "#   #", " ####", "    #", "    #", " ### " };
    case '/': return { "    #", "   # ", "   # ", "  #  ", " #   ", " #   ", "#    " };
    case '(': return { "   ##", "  #  ", " #   ", " #   ", " #   ", "  #  ", "   ##" };
    case ')': return { "##   ", "  #  ", "   # ", "   # ", "   # ", "  #  ", "##   " };
    case '[': return { "#####", "#    ", "#    ", "#    ", "#    ", "#    ", "#####" };
    case ']': return { "#####", "    #", "    #", "    #", "    #", "    #", "#####" };
    case '=': return { "     ", "     ", "#####", "     ", "#####", "     ", "     " };
    case '.': return { "     ", "     ", "     ", "     ", "     ", " ##  ", " ##  " };
    case ',': return { "     ", "     ", "     ", "     ", " ##  ", " ##  ", " #   " };
    case '-': return { "     ", "     ", "     ", "#####", "     ", "     ", "     " };
    case '+': return { "     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     " };
    case ':': return { "     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     " };
    case ' ': return { "     ", "     ", "     ", "     ", "     ", "     ", "     " };
    default: return { " ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  " };
    }
}

void uploadGlyph(std::map<char, Character>& characters, char code)
{
    constexpr int glyphWidth = 5;
    constexpr int glyphHeight = 7;
    constexpr int pixelScale = 3;
    constexpr int textureWidth = glyphWidth * pixelScale;
    constexpr int textureHeight = glyphHeight * pixelScale;

    const GlyphRows rows = glyphRows(code);
    std::vector<unsigned char> pixels(textureWidth * textureHeight, 0);

    for (int y = 0; y < glyphHeight; ++y) {
        for (int x = 0; x < glyphWidth; ++x) {
            if (rows[y][x] == ' ') continue;
            for (int py = 0; py < pixelScale; ++py) {
                for (int px = 0; px < pixelScale; ++px) {
                    const int outX = x * pixelScale + px;
                    const int outY = y * pixelScale + py;
                    pixels[outY * textureWidth + outX] = 255;
                }
            }
        }
    }

    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, textureWidth, textureHeight, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    Character character = {
        texture,
        glm::ivec2(textureWidth, textureHeight),
        glm::ivec2(0, textureHeight),
        static_cast<unsigned int>((glyphWidth + 1) * pixelScale * 64)
    };
    characters.insert({ code, character });
}
}
#endif

void TextRenderer::initRenderData()
{
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void TextRenderer::Load(std::string fontPath, unsigned int fontSize)
{
#ifdef __EMSCRIPTEN__
    (void)fontPath;
    (void)fontSize;
    Characters.clear();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int c = 32; c < 127; ++c) {
        uploadGlyph(Characters, static_cast<char>(c));
    }
    glBindTexture(GL_TEXTURE_2D, 0);
#else
    Characters.clear();

    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        std::cout << "ERROR::FREETYPE: Could not init FreeType Library\n";
        return;
    }

    FT_Face face;
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face)) {
        std::cout << "ERROR::FREETYPE: Failed to load font\n";
        FT_Done_FreeType(ft);
        return;
    }

    FT_Set_Pixel_Sizes(face, 0, fontSize);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (unsigned char c = 0; c < 128; ++c)
    {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            std::cout << "ERROR::FREETYPE: Failed to load Glyph: " << c << "\n";
            continue;
        }

        unsigned int texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);

        const int bitmapWidth = static_cast<int>(face->glyph->bitmap.width);
        const int bitmapRows = static_cast<int>(face->glyph->bitmap.rows);
        const int textureWidth = std::max(1, bitmapWidth);
        const int textureHeight = std::max(1, bitmapRows);
        const unsigned char transparentPixel = 0;
        const unsigned char* bitmapBuffer = (bitmapWidth > 0 && bitmapRows > 0)
            ? face->glyph->bitmap.buffer
            : &transparentPixel;

        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_R8,
            textureWidth,
            textureHeight,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            bitmapBuffer
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        Character character = {
            texture,
            glm::ivec2(bitmapWidth, bitmapRows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<unsigned int>(face->glyph->advance.x)
        };

        Characters.insert({ c, character });
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
#endif
}

void TextRenderer::RenderText(const std::string& text, float x, float y, float scale, glm::vec3 color)
{
    if (Characters.empty()) return;
    shader->use();
    shader->setVec3("textColor", color);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);

    for (auto c = text.begin(); c != text.end(); ++c)
    {
        const auto character = Characters.find(*c);
        if (character == Characters.end()) continue;
        Character ch = character->second;
        if (glIsTexture(ch.TextureID) == GL_FALSE) {
            std::cerr << "Invalid texture for character: " << *c << std::endl;
        }

        float xpos = x + ch.Bearing.x * scale;
        float ypos = y + (Characters['H'].Bearing.y - ch.Bearing.y) * scale;

        float w = ch.Size.x * scale;
        float h = ch.Size.y * scale;

        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 0.0f },
            { xpos,     ypos,       0.0f, 0.0f },
            { xpos,     ypos + h,   0.0f, 1.0f },
            { xpos + w, ypos + h,   1.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 0.0f }
        };

        glBindTexture(GL_TEXTURE_2D, ch.TextureID);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        x += (ch.Advance >> 6) * scale;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
void TextRenderer::RenderTextScale(const std::string& text, float x, float y, float targetWidth, float targetHeight, glm::vec3 color)
{
    if (Characters.empty()) return;
    // 1. Calculate the original size of the text at a scale of 1.0
    float naturalWidth = GetTextWidth(text, 1.0f);
    float naturalHeight = (float)Characters['H'].Size.y;

    if (naturalWidth == 0.0f || naturalHeight == 0.0f) return;

    // 2. Calculate the scale required for width and height separately
    float scaleX = targetWidth / naturalWidth;
    float scaleY = targetHeight / naturalHeight;

    // --- This is the main change: We no longer find the minimum scale. ---
    // We will use scaleX and scaleY independently.

    shader->use();
    shader->setVec3("textColor", color);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);

    float currentX = x; // Start at the beginning of the box

    for (auto c = text.begin(); c != text.end(); ++c)
    {
        const auto character = Characters.find(*c);
        if (character == Characters.end()) continue;
        Character ch = character->second;

        // Apply scaleX to horizontal properties and scaleY to vertical properties
        float xpos = currentX + ch.Bearing.x * scaleX;
        float ypos = y + (Characters['H'].Bearing.y - ch.Bearing.y) * scaleY;

        float w = ch.Size.x * scaleX;
        float h = ch.Size.y * scaleY;

        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 0.0f },
            { xpos,     ypos,       0.0f, 0.0f },
            { xpos,     ypos + h,   0.0f, 1.0f },
            { xpos + w, ypos + h,   1.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 0.0f }
        };

        glBindTexture(GL_TEXTURE_2D, ch.TextureID);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Advance cursor based on the horizontal scale
        currentX += (ch.Advance >> 6) * scaleX;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

float TextRenderer::GetTextWidth(const std::string& text, float scale) {
    if (Characters.empty()) return static_cast<float>(text.size()) * 12.0f * scale;
    float width = 0.0f;
    for (char c : text) {
        const auto character = Characters.find(c);
        if (character == Characters.end()) continue;
        Character ch = character->second;
        width += (ch.Advance >> 6) * scale;    // Bitshift back to pixels, scale applied
    }
    return width;
}
