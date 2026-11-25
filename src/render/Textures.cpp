#include "render/Textures.hpp"

#include <format>

#include "assets/EmbeddedAssets.hpp"
#include "assets/PngInfo.hpp"

namespace spindle::render {

namespace {

struct SurfaceDeleter {
    void operator()(SDL_Surface* s) const { SDL_DestroySurface(s); }
};

TexturePtr loadEmbeddedPng(SDL_Renderer* renderer, const char* path, std::string* error) {
    const auto data = assets::findEmbedded(path);
    if (!data) {
        *error = std::format("{} is not embedded", path);
        return nullptr;
    }
    if (!assets::readPngInfo(*data)) {
        *error = std::format("{} has a damaged PNG header", path);
        return nullptr;
    }
    SDL_IOStream* io = SDL_IOFromConstMem(data->data(), data->size());
    if (!io) {
        *error = std::format("{}: {}", path, SDL_GetError());
        return nullptr;
    }
    std::unique_ptr<SDL_Surface, SurfaceDeleter> surface(SDL_LoadPNG_IO(io, true));
    if (!surface) {
        *error = std::format("{}: {}", path, SDL_GetError());
        return nullptr;
    }
    TexturePtr tex(SDL_CreateTextureFromSurface(renderer, surface.get()));
    if (!tex) {
        *error = std::format("{}: {}", path, SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureBlendMode(tex.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex.get(), SDL_SCALEMODE_LINEAR);
    return tex;
}

} // namespace

bool Textures::load(SDL_Renderer* renderer, std::string* error) {
    release();
    if (!renderer) {
        *error = "no renderer";
        return false;
    }
    flame_ = loadEmbeddedPng(renderer, kFlameImage, error);
    return flame_ != nullptr;
}

void Textures::release() { flame_.reset(); }

} // namespace spindle::render
