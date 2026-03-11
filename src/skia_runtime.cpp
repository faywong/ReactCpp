#include "skia_runtime.hpp"

#include <cstring>
#include <vector>

#include "core/SkCanvas.h"
#include "core/SkColor.h"
#include "core/SkImageInfo.h"
#include "core/SkSurface.h"

#include <SDL2/SDL.h>

thread_local HookDispatcher g_skia_dispatcher;

TypeId host_type_view() {
    static int dummy;
    return &dummy;
}

TypeId host_type_rect() {
    static int dummy;
    return &dummy;
}

Element View(const std::vector<Element>& children) {
    Element e;
    e.type = host_type_view();
    e.children = children;
    return e;
}

Element Rect(float r, float g, float b) {
    Element e;
    e.type = host_type_rect();
    e.props.r = r;
    e.props.g = g;
    e.props.b = b;
    return e;
}

static void draw_element(const Element& el, SkCanvas* canvas, int width, int height) {
    if (el.type == host_type_view()) {
        for (const auto& child : el.children) {
            draw_element(child, canvas, width, height);
        }
    } else if (el.type == host_type_rect()) {
        const auto& p = el.props;
        SkPaint paint;
        paint.setColor(SkColorSetARGB(
            255,
            static_cast<U8CPU>(p.r * 255.0f),
            static_cast<U8CPU>(p.g * 255.0f),
            static_cast<U8CPU>(p.b * 255.0f)
        ));
        const float w = width * 0.5f;
        const float h = height * 0.5f;
        const float x = (width - w) * 0.5f;
        const float y = (height - h) * 0.5f;
        canvas->drawRect(SkRect::MakeXYWH(x, y, w, h), paint);
    }
}

void draw_element_tree(const Element& el, SkCanvas* canvas, int width, int height) {
    canvas->clear(SK_ColorWHITE);
    draw_element(el, canvas, width, height);
}

int run_skia_app(const AppRenderFunc& app) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error("SDL_Init failed");
    }

    const int width = 800;
    const int height = 600;

    SDL_Window* window = SDL_CreateWindow(
        "Skia Reactive Demo",
        width,
        height,
        SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        SDL_Quit();
        throw std::runtime_error("SDL_CreateWindow failed");
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error("SDL_CreateRenderer failed");
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height
    );
    if (!texture) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error("SDL_CreateTexture failed");
    }

    SkImageInfo info = SkImageInfo::Make(
        width,
        height,
        kRGBA_8888_SkColorType,
        kPremul_SkAlphaType
    );

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
    auto surface = SkSurface::MakeRasterDirect(
        info,
        pixels.data(),
        width * 4
    );
    if (!surface) {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        throw std::runtime_error("SkSurface::MakeRasterDirect failed");
    }

    InstanceNode root_instance;
    root_instance.type = host_type_view();

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e) == 1) {
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        g_skia_dispatcher.current_instance = &root_instance;
        g_skia_dispatcher.current_index = 0;

        Element root_vnode = app();
        root_instance.current_vnode = root_vnode;

        SkCanvas* canvas = surface->getCanvas();
        draw_element_tree(root_instance.current_vnode, canvas, width, height);

        void* texPixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(texture, nullptr, &texPixels, &pitch) != 0) {
            break;
        }

        for (int y = 0; y < height; ++y) {
            std::memcpy(
                static_cast<std::uint8_t*>(texPixels) + y * pitch,
                pixels.data() + static_cast<std::size_t>(y) * width,
                static_cast<std::size_t>(width) * 4
            );
        }

        SDL_UnlockTexture(texture);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
