#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace openhi2txt {

    class EntityMapper {
    public:
        /**
         * Replaces XML entities (e.g. &jp-h-a;) with their UTF-8 equivalents.
         */
        static std::string resolve(std::string text) {
            if (text.find('&') == std::string::npos) {
                return text;
            }

            static const std::unordered_map<std::string, std::string> entities = {
                // Standard XML
                {"quot", "\""}, {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"apos", "'"},

                // Basic Symbols
                {"big-mid-dot", "\u0095"}, {"copyright", "\u00A9"}, {"mid-dot", "\u00B7"},
                {"one-on-two", "\u00BD"}, {"ring", "\u00D6"}, {"acute", "\u00E1"},
                {"y-strike", "\u024E"}, {"bridge1", "\u02F9"}, {"bridge2", "\u02FD"},
                {"bridge3", "\u02FE"},

                // Greek Alphabet
                {"alpha", "\u03B1"}, {"beta", "\u03B2"}, {"gamma", "\u03B3"}, {"delta", "\u03B4"},
                {"epsilon", "\u03B5"}, {"zeta", "\u03B6"}, {"eta", "\u03B7"}, {"theta", "\u03B8"},
                {"iota", "\u03B9"}, {"kappa", "\u03BA"}, {"lambda", "\u03BB"}, {"mu", "\u03BC"},
                {"nu", "\u03BD"}, {"xi", "\u03BE"}, {"omicron", "\u03BF"}, {"pi", "\u03C0"},
                {"rho", "\u03C1"}, {"sigmaf", "\u03C2"}, {"sigma", "\u03C3"}, {"tau", "\u03C4"},
                {"upsilon", "\u03C5"}, {"phi", "\u03C6"}, {"chi", "\u03C7"}, {"psi", "\u03C8"},
                {"omega", "\u03C9"},

                // Punctuation & Icons
                {"three-dots", "\u2026"}, {"two-exclamations", "\u203C"}, {"broken-question", "\u2049"},
                {"asterism", "\u2042"}, {"w-double-strike", "\u20A9"}, {"square-2", "\u20DE"},

                // Roman Numerals (Upper)
                {"roman-numeral-1", "\u2160"}, {"roman-numeral-2", "\u2161"}, {"roman-numeral-3", "\u2162"},
                {"roman-numeral-4", "\u2163"}, {"roman-numeral-5", "\u2164"}, {"roman-numeral-6", "\u2165"},
                {"roman-numeral-7", "\u2166"}, {"roman-numeral-8", "\u2167"}, {"roman-numeral-9", "\u2168"},
                {"roman-numeral-10", "\u2169"}, {"roman-numeral-11", "\u216A"}, {"roman-numeral-12", "\u216B"},

                // Roman Numerals (Lower)
                {"small-roman-numeral-1", "\u2170"}, {"small-roman-numeral-2", "\u2171"}, {"small-roman-numeral-3", "\u2172"},
                {"small-roman-numeral-4", "\u2173"}, {"small-roman-numeral-5", "\u2174"}, {"small-roman-numeral-6", "\u2175"},
                {"small-roman-numeral-7", "\u2176"}, {"small-roman-numeral-8", "\u2177"}, {"small-roman-numeral-9", "\u2178"},
                {"small-roman-numeral-10", "\u2179"}, {"small-roman-numeral-11", "\u217A"}, {"small-roman-numeral-12", "\u217B"},

                // Arrows & UI
                {"left-arrow", "\u2190"}, {"right-double-arrow", "\u21D2"}, {"black-triangle-right", "\u25BA"},
                {"black-triangle-down", "\u25BC"}, {"square", "\u25A1"}, {"dot-in-square", "\u25A3"},

                // Symbols & Emojis (Mapped to Unicode)
                {"smiley", "\u263A"}, {"black-smiley", "\u263B"}, {"angry-face", "\u2639"},
                {"sun", "\u263C"}, {"moon", "\u263D"}, {"crescent-moon", "\u263E"},
                {"woman", "\u2640"}, {"man", "\u2642"}, {"star", "\u2606"}, {"black-star", "\u2605"},
                {"phone", "\u260E"}, {"hot-beverage", "\u2615"}, {"skull", "\u2620"},
                {"peace", "\u262E"}, {"wheelchair", "\u267F"}, {"single-music-note", "\u266A"},
                {"double-music-note", "\u266B"}, {"heart", "\u2661"}, {"black-heart", "\u2665"},
                {"black-spade", "\u2660"}, {"black-club", "\u2663"}, {"black-diamond", "\u2666"},

                // Japanese Hiragana (jp-h-*)
                {"jp-h-a-small", "\u3041"}, {"jp-h-a", "\u3042"}, {"jp-h-i-small", "\u3043"}, {"jp-h-i", "\u3044"},
                {"jp-h-u-small", "\u3045"}, {"jp-h-u", "\u3046"}, {"jp-h-e-small", "\u3047"}, {"jp-h-e", "\u3048"},
                {"jp-h-o-small", "\u3049"}, {"jp-h-o", "\u304A"}, {"jp-h-ka", "\u304B"}, {"jp-h-ga", "\u304C"},
                {"jp-h-ki", "\u304D"}, {"jp-h-gi", "\u304E"}, {"jp-h-ku", "\u304F"}, {"jp-h-gu", "\u3050"},
                {"jp-h-ke", "\u3051"}, {"jp-h-ge", "\u3052"}, {"jp-h-ko", "\u3053"}, {"jp-h-go", "\u3054"},
                {"jp-h-sa", "\u3055"}, {"jp-h-za", "\u3056"}, {"jp-h-si", "\u3057"}, {"jp-h-zi", "\u3058"},
                {"jp-h-su", "\u3059"}, {"jp-h-zu", "\u305A"}, {"jp-h-se", "\u305B"}, {"jp-h-ze", "\u305C"},
                {"jp-h-so", "\u305D"}, {"jp-h-zo", "\u305E"}, {"jp-h-ta", "\u305F"}, {"jp-h-da", "\u3060"},
                {"jp-h-ti", "\u3061"}, {"jp-h-di", "\u3062"}, {"jp-h-tu-small", "\u3063"}, {"jp-h-sokuon", "\u3063"},
                {"jp-h-tu", "\u3064"}, {"jp-h-du", "\u3065"}, {"jp-h-te", "\u3066"}, {"jp-h-de", "\u3067"},
                {"jp-h-to", "\u3068"}, {"jp-h-do", "\u3069"}, {"jp-h-na", "\u306A"}, {"jp-h-ni", "\u306B"},
                {"jp-h-nu", "\u306C"}, {"jp-h-ne", "\u306D"}, {"jp-h-no", "\u306E"}, {"jp-h-ha", "\u306F"},
                {"jp-h-ba", "\u3070"}, {"jp-h-pa", "\u3071"}, {"jp-h-hi", "\u3072"}, {"jp-h-bi", "\u3073"},
                {"jp-h-pi", "\u3074"}, {"jp-h-hu", "\u3075"}, {"jp-h-bu", "\u3076"}, {"jp-h-pu", "\u3077"},
                {"jp-h-he", "\u3078"}, {"jp-h-be", "\u3079"}, {"jp-h-pe", "\u307A"}, {"jp-h-ho", "\u307B"},
                {"jp-h-bo", "\u307C"}, {"jp-h-po", "\u307D"}, {"jp-h-ma", "\u307E"}, {"jp-h-mi", "\u307F"},
                {"jp-h-mu", "\u3080"}, {"jp-h-me", "\u3081"}, {"jp-h-mo", "\u3082"}, {"jp-h-ya-small", "\u3083"},
                {"jp-h-ya", "\u3084"}, {"jp-h-yu-small", "\u3085"}, {"jp-h-yu", "\u3086"}, {"jp-h-yo-small", "\u3087"},
                {"jp-h-yo", "\u3088"}, {"jp-h-ra", "\u3089"}, {"jp-h-ri", "\u308A"}, {"jp-h-ru", "\u308B"},
                {"jp-h-re", "\u308C"}, {"jp-h-ro", "\u308D"}, {"jp-h-wa-small", "\u308E"}, {"jp-h-wa", "\u308F"},
                {"jp-h-wi", "\u3090"}, {"jp-h-we", "\u3091"}, {"jp-h-wo", "\u3092"}, {"jp-h-vu", "\u3094"}, {"jp-h-n", "\u3093"},

                // Japanese Katakana (jp-k-*)
                {"jp-k-a-small", "\u30A1"}, {"jp-k-a", "\u30A2"}, {"jp-k-i-small", "\u30A3"}, {"jp-k-i", "\u30A4"},
                {"jp-k-u-small", "\u30A5"}, {"jp-k-u", "\u30A6"}, {"jp-k-e-small", "\u30A7"}, {"jp-k-e", "\u30A8"},
                {"jp-k-o-small", "\u30A9"}, {"jp-k-o", "\u30AA"}, {"jp-k-ka", "\u30AB"}, {"jp-k-ga", "\u30AC"},
                {"jp-k-ki", "\u30AD"}, {"jp-k-gi", "\u30AE"}, {"jp-k-ku", "\u30AF"}, {"jp-k-gu", "\u30B0"},
                {"jp-k-ke", "\u30B1"}, {"jp-k-ge", "\u30B2"}, {"jp-k-ko", "\u30B3"}, {"jp-k-go", "\u30B4"},
                {"jp-k-sa", "\u30B5"}, {"jp-k-za", "\u30B6"}, {"jp-k-si", "\u30B7"}, {"jp-k-zi", "\u30B8"},
                {"jp-k-su", "\u30B9"}, {"jp-k-zu", "\u30BA"}, {"jp-k-se", "\u30BB"}, {"jp-k-ze", "\u30BC"},
                {"jp-k-so", "\u30BD"}, {"jp-k-zo", "\u30BE"}, {"jp-k-ta", "\u30BF"}, {"jp-k-da", "\u30C0"},
                {"jp-k-ti", "\u30C1"}, {"jp-k-di", "\u30C2"}, {"jp-k-tu-small", "\u30C3"}, {"jp-k-tu", "\u30C4"},
                {"jp-k-du", "\u30C5"}, {"jp-k-te", "\u30C6"}, {"jp-k-de", "\u30C7"}, {"jp-k-to", "\u30C8"},
                {"jp-k-do", "\u30C9"}, {"jp-k-na", "\u30CA"}, {"jp-k-ni", "\u30CB"}, {"jp-k-nu", "\u30CC"},
                {"jp-k-ne", "\u30CD"}, {"jp-k-no", "\u30CE"}, {"jp-k-ha", "\u30CF"}, {"jp-k-ba", "\u30D0"},
                {"jp-k-pa", "\u30D1"}, {"jp-k-hi", "\u30D2"}, {"jp-k-bi", "\u30D3"}, {"jp-k-pi", "\u30D4"},
                {"jp-k-hu", "\u30D5"}, {"jp-k-bu", "\u30D6"}, {"jp-k-pu", "\u30D7"}, {"jp-k-he", "\u30D8"},
                {"jp-k-be", "\u30D9"}, {"jp-k-pe", "\u30DA"}, {"jp-k-ho", "\u30DB"}, {"jp-k-bo", "\u30DC"},
                {"jp-k-po", "\u30DD"}, {"jp-k-ma", "\u30DE"}, {"jp-k-mi", "\u30DF"}, {"jp-k-mu", "\u30E0"},
                {"jp-k-me", "\u30E1"}, {"jp-k-mo", "\u30E2"}, {"jp-k-ya-small", "\u30E3"}, {"jp-k-ya", "\u30E4"},
                {"jp-k-yu-small", "\u30E5"}, {"jp-k-yu", "\u30E6"}, {"jp-k-yo-small", "\u30E7"}, {"jp-k-yo", "\u30E8"},
                {"jp-k-ra", "\u30E9"}, {"jp-k-ri", "\u30EA"}, {"jp-k-ru", "\u30EB"}, {"jp-k-re", "\u30EC"},
                {"jp-k-ro", "\u30ED"}, {"jp-k-wa-small", "\u30EE"}, {"jp-k-wa", "\u30EF"}, {"jp-k-wi", "\u30F0"},
                {"jp-k-we", "\u30F1"}, {"jp-k-wo", "\u30F2"}, {"jp-k-n", "\u30F3"}, {"jp-k-vu", "\u30F4"},

                // Japanese Kanji Numbers
                {"jp-k-zero", "\u96F6"}, {"jp-k-one", "\u4E00"}, {"jp-k-two", "\u4E8C"}, {"jp-k-three", "\u4E09"},
                {"jp-k-four", "\u56DB"}, {"jp-k-five", "\u4E94"}, {"jp-k-six", "\u516D"}, {"jp-k-seven", "\u4E03"},
                {"jp-k-height", "\u516B"}, {"jp-k-nine", "\u4E5D"},

                // Special UI/Game Icons
                {"thumbs-up", "\u221A"}, {"feet", "\U0001F43E"}, {"heart-with-arrow", "\u2665"},
                {"mans-symbol", "\u2642"}, {"mens-symbol", "\u2642"}, {"womens-symbol", "\u2640"},

                // Additional entities declared by the official hi2txt.dtd
                {"jp-h-handakuten", "\u309C"}, {"jp-h-dakuten", "\u309B"},
                {"jp-h-youon-a", "\u3083"}, {"jp-h-youon-u", "\u3085"}, {"jp-h-youon-o", "\u3087"},
                {"spaceship", "\u2646"}, {"headset", "\u260A"},
                {"aries", "\u2648"}, {"taurus", "\u2649"}, {"gemini", "\u264A"},
                {"cancer", "\u264B"}, {"leo", "\u264C"}, {"virgo", "\u264D"},
                {"libra", "\u264E"}, {"scorpio", "\u264F"}, {"sagitarius", "\u2650"},
                {"capricorn", "\u2651"}, {"aquarius", "\u2652"}, {"pisces", "\u2653"},
                {"left-foot", "\u2308"}, {"right-foot", "\u2308"},
                {"airplane", "\u2708"}, {"scissors", "\u2704"},
                {"black-right-arrow-large", "\u27A8"}, {"crossed-swords", "\u2694"},
                {"car-side", "\u26D0"}, {"car-front", "\u26D0"},
                {"cross-of-jerusalem", "\u2629"}, {"cross-of-lorraine", "\u2628"},
                {"multiplication", "\u2715"}, {"two-cubes", "\u25EB"},
                {"four-lines", "\u2263"}, {"round-7", "\u2466"},
                {"cat-face", "\u263A"}, {"big-dot", "\u2022"}, {"amber", "\u2658"},
                {"big-exclamation", "\u2762"}, {"two-dots", "\u0589"},
                {"inverted-question", "\u061F"}, {"baseball", "\u26BE"},
                {"snowman", "\u2603"}, {"umbrella", "\u2602"},
                {"three-mid-dots", "\u22EF"}, {"boat", "\u26F5"},
                {"kiss", "\u263A"}, {"rdquo", "\u201D"},
                {"slash-in-square", "\u25A7"}, {"antislash-in-square", "\u25A8"},
                {"h-lines-in-square", "\u25A4"},
                {"lama", "x"}, {"whale", "x"}, {"shoe", "x"}
            };

            std::string result = text;
            size_t pos = 0;
            while ((pos = result.find('&', pos)) != std::string::npos) {
                size_t end = result.find(';', pos);
                if (end == std::string::npos) break;

                std::string entityName = result.substr(pos + 1, end - pos - 1);

                // Handle numeric entities directly (&#1234;)
                if (!entityName.empty() && entityName[0] == '#') {
                    try {
                        int code = 0;
                        if (entityName.size() > 1 && (entityName[1] == 'x' || entityName[1] == 'X'))
                            code = std::stoi(entityName.substr(2), nullptr, 16);
                        else
                            code = std::stoi(entityName.substr(1), nullptr, 10);

                        std::string utf8;
                        if (code <= 0x7F) utf8 += (char)code;
                        else if (code <= 0x7FF) {
                            utf8 += (char)(0xC0 | (code >> 6));
                            utf8 += (char)(0x80 | (code & 0x3F));
                        }
                        else if (code <= 0xFFFF) {
                            utf8 += (char)(0xE0 | (code >> 12));
                            utf8 += (char)(0x80 | ((code >> 6) & 0x3F));
                            utf8 += (char)(0x80 | (code & 0x3F));
                        }
                        // (Omitted 4-byte UTF8 for brevity, hi2txt rarely needs it)

                        result.replace(pos, end - pos + 1, utf8);
                        pos += utf8.length();
                        continue;
                    }
                    catch (...) { /* ignore malformed */ }
                }

                // Handle named entities from map
                auto it = entities.find(entityName);
                if (it != entities.end()) {
                    result.replace(pos, end - pos + 1, it->second);
                    pos += it->second.length();
                }
                else {
                    pos = end + 1;
                }
            }
            return result;
        }
    };

} // namespace openhi2txt
