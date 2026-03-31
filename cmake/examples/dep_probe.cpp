#include <cxxopts.hpp>
#include <FastNoiseLite.h>
#include <nlohmann/json.hpp>
#include <rapidxml.hpp>
#include <rect_structs.h>

int main()
{
    cxxopts::Options options("hex-dep-probe", "Dependency probe");
    options.add_options()("help", "Show help");

    FastNoiseLite noise;
    noise.SetFrequency(0.01f);
    const float value = noise.GetNoise(1.0f, 2.0f);

    nlohmann::json payload;
    payload["noise"] = value;
    payload["ok"] = true;

    rapidxml::xml_document<> doc;
    (void)doc;

    return payload["ok"].get<bool>() ? 0 : 1;
}
