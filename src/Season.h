#pragma once

// Сезон оверлея.
//   Winter — снег, сугробы, снеговики, снегоуборщик.
//   Autumn — дождь и/или падающие листья (мышь не перехватывается).
enum class Season { Winter, Autumn };

// Что происходит осенью. Хотя бы одно поле должно быть true: «пустая» осень
// бессмысленна, поэтому меню не даёт снять последнюю галочку, а SnowWindow
// такую комбинацию игнорирует.
struct AutumnOptions {
    bool rain = false;
    bool leaves = true;

    bool operator==(const AutumnOptions &other) const
    {
        return rain == other.rain && leaves == other.leaves;
    }
    bool operator!=(const AutumnOptions &other) const { return !(*this == other); }
};
