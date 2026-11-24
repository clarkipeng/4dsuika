#include "text_renderer.h"

#include <algorithm>
#include <cmath>

// One baked glyph before packing: an 8-bit coverage bitmap and its metrics in pixels.
struct GlyphBitmap {
    uint32_t code = 0;
    int w = 0, h = 0, left = 0, top = 0;
    float advance = 0.0f;
    std::vector<unsigned char> pixels;
};

// Codepoints baked into the atlas: ASCII plus the few typographic marks the UI uses.
static std::vector<uint32_t> atlasCodepoints() {
    std::vector<uint32_t> cps;
    for (uint32_t c = 32; c < 127; ++c) cps.push_back(c);
    for (uint32_t c : {0xD7u, 0xB7u, 0x2013u, 0x2014u, 0x2212u, 0x2026u, 0xB0u}) cps.push_back(c);
    return cps;
}

// Decodes one UTF-8 sequence at s[i], advancing i. Bad bytes become '?'.
static uint32_t nextCodepoint(const std::string& s, size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (extra < 0) return '?';
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra; ++k) {
        if (i >= s.size()) return '?';
        cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    }
    return cp;
}


static const int kMaxPolyVerts = 16;
static const float kNominal = 24.0f;   // text scale 1 = a 24 px font

TextRenderer::~TextRenderer()
{
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    if (whiteTex) glDeleteTextures(1, &whiteTex);
    for (unsigned int t : atlasTex) if (t) glDeleteTextures(1, &t);
}

void TextRenderer::initRenderData()
{
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    vboBytes = sizeof(float) * kMaxPolyVerts * 3 * 4;
    glBufferData(GL_ARRAY_BUFFER, vboBytes, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // 1x1 texture whose red channel is 1, so the text shader paints solid colour.
    unsigned char white = 255;
    glGenTextures(1, &whiteTex);
    glBindTexture(GL_TEXTURE_2D, whiteTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Uploads one draw's vertices into the bound VBO. Orphaning the storage first
// lets the driver hand out fresh memory instead of waiting on (and on macOS
// flushing) earlier draws that still read the buffer.
void TextRenderer::stream(const std::vector<float>& v)
{
    size_t bytes = v.size() * sizeof(float);
    vboBytes = std::max(vboBytes, bytes);
    glBufferData(GL_ARRAY_BUFFER, vboBytes, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, v.data());
}

void TextRenderer::Begin(float width, float height)
{
    glm::mat4 projection = glm::ortho(0.0f, width, height, 0.0f);
    shader->use();
    shader->setMat4("projection", projection);
    shader->setInt("text", 0);
    if (shapeShader_) {
        shapeShader_->use();
        shapeShader_->setMat4("projection", projection);
    }
}

const Character* TextRenderer::glyph(uint32_t code) const
{
    const auto& m = Characters[face_];
    auto it = m.find(code);
    if (it != m.end()) return &it->second;
    if (code == 0x2212) return glyph('-');
    it = m.find('?');
    return it == m.end() ? nullptr : &it->second;
}

float TextRenderer::CapHeight(float scale)
{
    const Character* h = glyph('H');
    return h ? h->Size.y * scale : 18.0f * scale;
}

void TextRenderer::FillPoly(const std::vector<glm::vec2>& pts, glm::vec3 color, float alpha)
{
    int n = int(pts.size());
    if (n < 3) return;
    std::vector<float> v;
    for (int i = 1; i + 1 < n; ++i)
        for (const glm::vec2& p : {pts[0], pts[i], pts[i + 1]}) v.insert(v.end(), {p.x, p.y, 0.0f, 0.0f});
    shader->use();
    shader->setVec3("textColor", color);
    shader->setFloat("textAlpha", alpha);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTex);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    stream(v);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(v.size() / 4));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TextRenderer::Shape(const UiShape& s)
{
    if (!shapeShader_ || s.alpha <= 0.0f) return;
    // Screen bounds of the shape, grown by the feather.
    glm::vec2 lo, hi;
    switch (s.mode) {
        case UiShape::RoundRect: lo = s.a - s.b; hi = s.a + s.b; break;
        case UiShape::Capsule: lo = glm::min(s.a, s.b) - s.radius; hi = glm::max(s.a, s.b) + s.radius; break;
        case UiShape::Arc: lo = s.a - glm::vec2(s.radius + s.border); hi = s.a + glm::vec2(s.radius + s.border); break;
        default: lo = glm::min(s.a, glm::min(s.b, s.c)) - s.radius; hi = glm::max(s.a, glm::max(s.b, s.c)) + s.radius; break;
    }
    const float grow = s.feather + 1.0f;
    glm::vec2 q0 = lo - grow, q1 = hi + grow;
    std::vector<float> v = {q0.x, q0.y, q0.x, q0.y, q1.x, q0.y, q1.x, q0.y, q1.x, q1.y, q1.x, q1.y,
                            q0.x, q0.y, q0.x, q0.y, q1.x, q1.y, q1.x, q1.y, q0.x, q1.y, q0.x, q1.y};
    Shader& sh = *shapeShader_;
    sh.use();
    sh.setInt("uMode", s.mode);
    sh.setVec2("uA", s.a);
    sh.setVec2("uB", s.b);
    sh.setVec2("uC", s.c);
    sh.setFloat("uRadius", s.radius);
    sh.setFloat("uBorder", s.border);
    sh.setVec2("uArc", s.arc);
    sh.setFloat("uFeather", std::max(s.feather, 0.5f));
    sh.setVec3("uTop", s.top);
    sh.setVec3("uBottom", s.bottom);
    sh.setVec2("uSpan", glm::vec2(lo.y, std::max(hi.y - lo.y, 1e-3f)));
    sh.setFloat("uAlpha", s.alpha);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    stream(v);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void TextRenderer::LoadFace(int face, const std::string& fontPath, unsigned int pixelSize)
{
    if (face < 0 || face >= kFaces) return;
    std::vector<GlyphBitmap> glyphs;
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) { std::cout << "ERROR::FREETYPE: Could not init FreeType Library\n"; return; }
    FT_Face ftFace;
    if (FT_New_Face(ft, fontPath.c_str(), 0, &ftFace)) {
        std::cout << "ERROR::FREETYPE: Failed to load font " << fontPath << "\n";
        FT_Done_FreeType(ft);
        return;
    }
    FT_Set_Pixel_Sizes(ftFace, 0, pixelSize);
    for (uint32_t c : atlasCodepoints()) {
        if (FT_Get_Char_Index(ftFace, c) == 0 && c >= 128) continue;
        if (FT_Load_Char(ftFace, c, FT_LOAD_RENDER)) continue;
        const FT_GlyphSlot gs = ftFace->glyph;
        GlyphBitmap g;
        g.code = c;
        g.w = int(gs->bitmap.width); g.h = int(gs->bitmap.rows);
        g.left = gs->bitmap_left; g.top = gs->bitmap_top;
        g.advance = float(gs->advance.x) / 64.0f;
        g.pixels.resize(size_t(g.w) * g.h);
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x) g.pixels[size_t(y) * g.w + x] = gs->bitmap.buffer[y * gs->bitmap.pitch + x];
        glyphs.push_back(std::move(g));
    }
    FT_Done_Face(ftFace);
    FT_Done_FreeType(ft);
    const float norm = kNominal / float(pixelSize);

    // Shelf-pack into one texture, with padding so mip levels do not bleed.
    const int pad = 8;
    int atlasW = 1024, x = pad, y = pad, rowH = 0;
    std::vector<glm::ivec2> pos(glyphs.size());
    for (int pass = 0; pass < 2; ++pass) {
        x = pad; y = pad; rowH = 0;
        for (size_t i = 0; i < glyphs.size(); ++i) {
            if (x + glyphs[i].w + pad > atlasW) { x = pad; y += rowH + pad; rowH = 0; }
            pos[i] = {x, y};
            x += glyphs[i].w + pad;
            rowH = std::max(rowH, glyphs[i].h);
        }
        if (y + rowH + pad <= atlasW) break;
        atlasW *= 2;   // big bake: try a wider sheet
    }
    int atlasH = 64;
    while (atlasH < y + rowH + pad) atlasH *= 2;
    std::vector<unsigned char> sheet(size_t(atlasW) * atlasH, 0);
    Characters[face].clear();
    for (size_t i = 0; i < glyphs.size(); ++i) {
        const GlyphBitmap& g = glyphs[i];
        for (int r = 0; r < g.h; ++r)
            std::copy(g.pixels.begin() + size_t(r) * g.w, g.pixels.begin() + size_t(r + 1) * g.w,
                      sheet.begin() + size_t(pos[i].y + r) * atlasW + pos[i].x);
        Character ch;
        ch.Size = glm::vec2(g.w, g.h) * norm;
        ch.Bearing = glm::vec2(g.left, g.top) * norm;
        ch.Advance = g.advance * norm;
        ch.uv = glm::vec4(float(pos[i].x) / atlasW, float(pos[i].y) / atlasH,
                          float(pos[i].x + g.w) / atlasW, float(pos[i].y + g.h) / atlasH);
        Characters[face][g.code] = ch;
    }
    if (!atlasTex[face]) glGenTextures(1, &atlasTex[face]);
    glBindTexture(GL_TEXTURE_2D, atlasTex[face]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, sheet.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TextRenderer::RenderText(const std::string& text, float x, float y, float scale, glm::vec3 color, float alpha)
{
    if (Characters[face_].empty() || alpha <= 0.0f) return;
    const Character* H = glyph('H');
    const float capTop = H ? H->Bearing.y : 0.0f;
    std::vector<float> v;
    v.reserve(text.size() * 24);
    for (size_t i = 0; i < text.size();) {
        const Character* ch = glyph(nextCodepoint(text, i));
        if (!ch) continue;
        float xpos = x + ch->Bearing.x * scale;
        float ypos = y + (capTop - ch->Bearing.y) * scale;
        float w = ch->Size.x * scale, h = ch->Size.y * scale;
        if (w > 0.0f && h > 0.0f) {
            const glm::vec4& t = ch->uv;
            v.insert(v.end(), {xpos, ypos + h, t.x, t.w,  xpos + w, ypos, t.z, t.y,  xpos, ypos, t.x, t.y,
                               xpos, ypos + h, t.x, t.w,  xpos + w, ypos + h, t.z, t.w,  xpos + w, ypos, t.z, t.y});
        }
        x += ch->Advance * scale;
    }
    if (v.empty()) return;
    shader->use();
    shader->setVec3("textColor", color);
    shader->setFloat("textAlpha", alpha);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTex[face_]);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    stream(v);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(v.size() / 4));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Stretches text to a box (non-uniform scale).
void TextRenderer::RenderTextScale(const std::string& text, float x, float y, float targetWidth, float targetHeight, glm::vec3 color)
{
    float nw = GetTextWidth(text, 1.0f), nh = CapHeight(1.0f);
    if (nw <= 0.0f || nh <= 0.0f) return;
    // Uniform fit is close enough for the few callers (sheet titles).
    float s = std::min(targetWidth / nw, targetHeight / nh);
    RenderText(text, x, y, s, color);
}

float TextRenderer::GetTextWidth(const std::string& text, float scale)
{
    if (Characters[face_].empty()) return float(text.size()) * 12.0f * scale;
    float width = 0.0f;
    for (size_t i = 0; i < text.size();) {
        const Character* ch = glyph(nextCodepoint(text, i));
        if (ch) width += ch->Advance * scale;
    }
    return width;
}
