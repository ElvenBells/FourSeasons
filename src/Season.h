#pragma once

enum class Season { Winter, Spring, Summer, Autumn };
enum class SeasonTool { Passive, Broom, Rake, Plant, Water, Acorn };

struct AutumnOptions {
    bool rain = false;
    bool leaves = true;
};
