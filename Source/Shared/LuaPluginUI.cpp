#include "Shared/LuaPluginUI.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace ts
{
namespace
{
constexpr const char* viewKey = "tstudio.view";
constexpr const char* gfxKey = "tstudio.gfx";

juce::Colour parseColour(const juce::var& value, juce::Colour fallback)
{
    const auto text = value.toString().trim();
    if (text.startsWith("#"))
    {
        const auto hex = text.substring(1);
        return hex.length() == 6 ? juce::Colour(0xff000000u | juce::uint32(hex.getHexValue32())) : juce::Colour(juce::uint32(hex.getHexValue32()));
    }
    return fallback;
}

juce::Colour colourArgument(lua_State* state, int index)
{
    if (lua_type(state, index) == LUA_TSTRING)
        return parseColour(juce::String::fromUTF8(lua_tostring(state, index)), juce::Colours::white);
    const auto component = [state](int i, double fallback) { return juce::uint8(juce::jlimit(0.0, 255.0, luaL_optnumber(state, i, fallback))); };
    return juce::Colour(component(index, 255), component(index + 1, 255), component(index + 2, 255), component(index + 3, 255));
}

float number(lua_State* state, int index, double fallback = 0.0)
{
    return float(luaL_optnumber(state, index, fallback));
}

juce::Justification justification(const juce::String& name)
{
    if (name == "left")
        return juce::Justification::centredLeft;
    if (name == "right")
        return juce::Justification::centredRight;
    return juce::Justification::centred;
}

class XYPad final : public juce::Component
{
public:
    XYPad(juce::RangedAudioParameter& x, juce::RangedAudioParameter& y, juce::Colour colour)
        : px(x), py(y), accent(colour),
          ax(x, [this](float) { repaint(); }), ay(y, [this](float) { repaint(); })
    {
        ax.sendInitialUpdate();
        ay.sendInitialUpdate();
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xff121618));
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(juce::Colour(0xff252b2f));
        for (int i = 1; i < 4; ++i)
        {
            g.drawHorizontalLine(juce::roundToInt(area.getY() + area.getHeight() * float(i) / 4.0f), area.getX(), area.getRight());
            g.drawVerticalLine(juce::roundToInt(area.getX() + area.getWidth() * float(i) / 4.0f), area.getY(), area.getBottom());
        }
        const auto point = juce::Point<float>(area.getX() + px.getValue() * area.getWidth(), area.getBottom() - py.getValue() * area.getHeight());
        g.setColour(accent.withAlpha(0.25f));
        g.fillEllipse(juce::Rectangle<float>(28.0f, 28.0f).withCentre(point));
        g.setColour(accent);
        g.fillEllipse(juce::Rectangle<float>(14.0f, 14.0f).withCentre(point));
        g.setColour(juce::Colours::black);
        g.drawRoundedRectangle(area, 6.0f, 1.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        ax.beginGesture();
        ay.beginGesture();
        mouseDrag(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        const auto nx = juce::jlimit(0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());
        const auto ny = juce::jlimit(0.0f, 1.0f, (area.getBottom() - e.position.y) / area.getHeight());
        ax.setValueAsPartOfGesture(px.convertFrom0to1(nx));
        ay.setValueAsPartOfGesture(py.convertFrom0to1(ny));
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        ax.endGesture();
        ay.endGesture();
    }

private:
    juce::RangedAudioParameter &px, &py;
    juce::Colour accent;
    juce::ParameterAttachment ax, ay;
};

class Meter final : public juce::Component
{
public:
    Meter(LuaPluginHost& h, int i, juce::Colour c, bool horizontal) : host(h), index(i), colour(c), sideways(horizontal) {}

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        level = juce::jmax(juce::jlimit(0.0f, 1.0f, host.meterValue(index)), level * 0.85f);
        g.setColour(juce::Colour(0xff121618));
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour(colour);
        g.fillRoundedRectangle(sideways ? area.withWidth(area.getWidth() * level) : area.withTrimmedTop(area.getHeight() * (1.0f - level)), 3.0f);
    }

private:
    LuaPluginHost& host;
    int index;
    juce::Colour colour;
    bool sideways;
    float level = 0.0f;
};

class Choice final : public juce::ComboBox
{
public:
    Choice(juce::RangedAudioParameter& p, const juce::StringArray& choices)
        : parameter(p), attachment(p, [this](float value) { setSelectedItemIndex(juce::roundToInt(value - parameter.getNormalisableRange().start), juce::dontSendNotification); })
    {
        addItemList(choices, 1);
        onChange = [this] { attachment.setValueAsCompleteGesture(parameter.getNormalisableRange().start + float(getSelectedItemIndex())); };
        attachment.sendInitialUpdate();
    }

private:
    juce::RangedAudioParameter& parameter;
    juce::ParameterAttachment attachment;
};

juce::StringArray choicesFor(const LuaPluginInfo& info, juce::RangedAudioParameter& parameter)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(&parameter))
        return choice->choices;
    for (const auto& entry : info.parameters)
        if (entry.id == parameter.getParameterID() || entry.name == parameter.getName(64))
            return entry.choices;
    return {};
}

juce::String unitFor(const LuaPluginInfo& info, juce::RangedAudioParameter& parameter)
{
    for (const auto& entry : info.parameters)
        if (entry.id == parameter.getParameterID() || entry.name == parameter.getName(64))
            return entry.unit;
    return parameter.getLabel();
}

class Toggle final : public juce::TextButton
{
public:
    Toggle(juce::RangedAudioParameter& p, const juce::String& text, juce::Colour colour)
        : TextButton(text), parameter(p), attachment(p, [this](float value) { setToggleState(value > (parameter.getNormalisableRange().start + parameter.getNormalisableRange().end) * 0.5f, juce::dontSendNotification); })
    {
        setClickingTogglesState(true);
        setColour(juce::TextButton::buttonOnColourId, colour);
        onClick = [this]
        {
            const auto& range = parameter.getNormalisableRange();
            attachment.setValueAsCompleteGesture(getToggleState() ? range.end : range.start);
        };
        attachment.sendInitialUpdate();
    }

private:
    juce::RangedAudioParameter& parameter;
    juce::ParameterAttachment attachment;
};
}

struct LuaPluginView::Widget
{
    std::unique_ptr<juce::Component> component;
    std::unique_ptr<juce::Label> label;
    std::unique_ptr<juce::SliderParameterAttachment> sliderAttachment;
};

struct GfxBinding
{
    static GfxScope* target(lua_State* state)
    {
        lua_getfield(state, LUA_REGISTRYINDEX, gfxKey);
        auto* scope = static_cast<GfxScope*>(lua_touserdata(state, -1));
        lua_pop(state, 1);
        if (scope == nullptr)
            luaL_error(state, "gfx functions can only be used inside draw()");
        return scope;
    }

    static juce::Graphics* graphics(lua_State* state) { return &target(state)->graphics; }

    static int colour(lua_State* state)
    {
        graphics(state)->setColour(colourArgument(state, 1));
        return 0;
    }

    static int fillRect(lua_State* state)
    {
        const juce::Rectangle<float> area(number(state, 1), number(state, 2), number(state, 3), number(state, 4));
        graphics(state)->fillRoundedRectangle(area, number(state, 5));
        return 0;
    }

    static int rect(lua_State* state)
    {
        const juce::Rectangle<float> area(number(state, 1), number(state, 2), number(state, 3), number(state, 4));
        graphics(state)->drawRoundedRectangle(area, number(state, 6), number(state, 5, 1.0));
        return 0;
    }

    static int fillCircle(lua_State* state)
    {
        const auto radius = number(state, 3);
        graphics(state)->fillEllipse(number(state, 1) - radius, number(state, 2) - radius, radius * 2.0f, radius * 2.0f);
        return 0;
    }

    static int circle(lua_State* state)
    {
        const auto radius = number(state, 3);
        graphics(state)->drawEllipse(number(state, 1) - radius, number(state, 2) - radius, radius * 2.0f, radius * 2.0f, number(state, 4, 1.0));
        return 0;
    }

    static int line(lua_State* state)
    {
        graphics(state)->drawLine(number(state, 1), number(state, 2), number(state, 3), number(state, 4), number(state, 5, 1.0));
        return 0;
    }

    static int text(lua_State* state)
    {
        auto* g = graphics(state);
        g->setFont(juce::FontOptions(number(state, 6, 13.0), lua_toboolean(state, 8) ? juce::Font::bold : juce::Font::plain));
        g->drawText(juce::String::fromUTF8(luaL_checkstring(state, 1)), juce::Rectangle<float>(number(state, 2), number(state, 3), number(state, 4), number(state, 5)),
                    justification(juce::String::fromUTF8(luaL_optstring(state, 7, "center"))), true);
        return 0;
    }

    static int arc(lua_State* state)
    {
        juce::Path path;
        const auto radius = number(state, 3);
        path.addCentredArc(number(state, 1), number(state, 2), radius, radius, 0.0f, number(state, 4), number(state, 5), true);
        graphics(state)->strokePath(path, juce::PathStrokeType(number(state, 6, 2.0), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return 0;
    }

    static juce::Path points(lua_State* state)
    {
        luaL_checktype(state, 1, LUA_TTABLE);
        juce::Path path;
        const auto count = lua_Integer(lua_rawlen(state, 1));
        for (lua_Integer i = 1; i + 1 <= count; i += 2)
        {
            lua_rawgeti(state, 1, i);
            lua_rawgeti(state, 1, i + 1);
            const auto x = float(lua_tonumber(state, -2));
            const auto y = float(lua_tonumber(state, -1));
            lua_pop(state, 2);
            if (i == 1)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        return path;
    }

    static int polyline(lua_State* state)
    {
        graphics(state)->strokePath(points(state), juce::PathStrokeType(number(state, 2, 1.5), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return 0;
    }

    static int fillPoly(lua_State* state)
    {
        auto path = points(state);
        path.closeSubPath();
        graphics(state)->fillPath(path);
        return 0;
    }

    static int gradient(lua_State* state)
    {
        graphics(state)->setGradientFill(juce::ColourGradient(colourArgument(state, 3), number(state, 1), number(state, 2), colourArgument(state, 6), number(state, 4),
                                                             number(state, 5), false));
        return 0;
    }

    static int knob(lua_State* state)
    {
        auto* g = graphics(state);
        const auto size = number(state, 3, 48.0);
        const auto value = juce::jlimit(0.0f, 1.0f, number(state, 4));
        const juce::Rectangle<float> area(number(state, 1), number(state, 2), size, size);
        const auto centre = area.getCentre();
        const auto radius = size * 0.5f;
        const auto start = juce::MathConstants<float>::pi * 1.25f;
        const auto end = juce::MathConstants<float>::pi * 2.75f;
        const auto angle = start + value * (end - start);
        const auto accent = target(state)->accent;
        juce::Path track, fill;
        track.addCentredArc(centre.x, centre.y, radius * 0.86f, radius * 0.86f, 0.0f, start, end, true);
        fill.addCentredArc(centre.x, centre.y, radius * 0.86f, radius * 0.86f, 0.0f, start, angle, true);
        g->setColour(juce::Colour(0xff121618));
        g->strokePath(track, juce::PathStrokeType(radius * 0.18f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g->setColour(accent);
        g->strokePath(fill, juce::PathStrokeType(radius * 0.18f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g->setGradientFill(juce::ColourGradient(juce::Colour(0xff4c565b), centre.x, area.getY(), juce::Colour(0xff23292c), centre.x, area.getBottom(), false));
        g->fillEllipse(area.reduced(radius * 0.32f));
        g->setColour(juce::Colour(0xffe8edf0));
        g->drawLine(juce::Line<float>(centre.getPointOnCircumference(radius * 0.18f, angle), centre.getPointOnCircumference(radius * 0.55f, angle)), 2.0f);
        return 0;
    }
};

void openGfx(lua_State* state)
{
    const luaL_Reg functions[] = {
        { "color", GfxBinding::colour }, { "fill_rect", GfxBinding::fillRect }, { "rect", GfxBinding::rect }, { "fill_circle", GfxBinding::fillCircle },
        { "circle", GfxBinding::circle }, { "line", GfxBinding::line }, { "text", GfxBinding::text }, { "arc", GfxBinding::arc },
        { "polyline", GfxBinding::polyline }, { "fill_poly", GfxBinding::fillPoly }, { "gradient", GfxBinding::gradient }, { "knob", GfxBinding::knob },
        { nullptr, nullptr },
    };
    luaL_newlib(state, functions);
    lua_setglobal(state, "gfx");
}

GfxScope::GfxScope(lua_State* s, juce::Graphics& g, juce::Colour colour) : state(s), graphics(g), accent(colour)
{
    lua_pushlightuserdata(state, this);
    lua_setfield(state, LUA_REGISTRYINDEX, gfxKey);
}

GfxScope::~GfxScope()
{
    lua_pushnil(state);
    lua_setfield(state, LUA_REGISTRYINDEX, gfxKey);
}

LuaPluginView* LuaPluginView::from(lua_State* state)
{
    lua_getfield(state, LUA_REGISTRYINDEX, viewKey);
    auto* view = static_cast<LuaPluginView*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return view;
}

juce::Rectangle<int> LuaPluginView::preferredSize(const LuaPluginInfo& info)
{
    return { 0, 0, int(info.ui.getProperty("width", 560)), int(info.ui.getProperty("height", 300)) };
}

LuaPluginView::LuaPluginView(const juce::String& code, const juce::String& origin, const LuaPluginInfo& i, LuaPluginHost& h)
    : info(i), host(h), background(parseColour(i.ui.getProperty("background", "#22282b"), juce::Colour(0xff22282b))),
      accent(parseColour(i.ui.getProperty("accent", "#e8483b"), juce::Colour(0xffe8483b)))
{
    state = luaL_newstate();
    openPluginLibraries(state);
    lua_pushlightuserdata(state, this);
    lua_setfield(state, LUA_REGISTRYINDEX, viewKey);
    lua_pushnumber(state, 48000.0);
    lua_setglobal(state, "sample_rate");

    openGfx(state);
    lua_pushcfunction(state, setParam);
    lua_setglobal(state, "set_param");
    lua_pushcfunction(state, getParam);
    lua_setglobal(state, "get_param");

    const auto utf8 = code.toStdString();
    armLuaWatchdog(state, 3000);
    if (luaL_loadbuffer(state, utf8.data(), utf8.size(), ("=" + origin).toRawUTF8()) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK)
    {
        error = juce::String::fromUTF8(lua_tostring(state, -1));
        lua_pop(state, 1);
    }
    lua_getglobal(state, "draw");
    hasDraw = lua_isfunction(state, -1);
    lua_pop(state, 1);

    buildWidgets();
    setSize(preferredSize(info).getWidth(), preferredSize(info).getHeight());
    startTimerHz(30);
}

LuaPluginView::~LuaPluginView()
{
    widgets.clear();
    lua_close(state);
}

void LuaPluginView::buildWidgets()
{
    const auto* list = info.ui.getProperty("widgets", {}).getArray();
    if (list == nullptr)
        return;
    for (const auto& spec : *list)
    {
        auto widget = std::make_unique<Widget>();
        const auto type = spec.getProperty("type", "knob").toString();
        const juce::Rectangle<int> bounds(int(spec.getProperty("x", 0)), int(spec.getProperty("y", 0)), int(spec.getProperty("w", 64)), int(spec.getProperty("h", 64)));
        const auto colour = parseColour(spec.getProperty("color", {}), accent);
        auto* parameter = host.parameterFor(spec.getProperty("param", {}).toString());
        const auto caption = spec.getProperty("label", parameter != nullptr ? juce::var(parameter->getName(32)) : juce::var()).toString();
        auto area = bounds;

        if (type == "label")
        {
            auto label = std::make_unique<juce::Label>(juce::String(), spec.getProperty("text", "").toString());
            label->setFont(juce::FontOptions(float(double(spec.getProperty("size", 14.0))), bool(spec.getProperty("bold", false)) ? juce::Font::bold : juce::Font::plain));
            label->setColour(juce::Label::textColourId, parseColour(spec.getProperty("color", {}), juce::Colour(0xffd6dde0)));
            label->setJustificationType(justification(spec.getProperty("align", "left").toString()));
            widget->component = std::move(label);
        }
        else if (type == "meter")
        {
            widget->component = std::make_unique<Meter>(host, info.meters.indexOf(spec.getProperty("meter", "").toString()), colour, bounds.getWidth() > bounds.getHeight());
        }
        else if (type == "scope")
        {
            widget->component = host.createScope();
        }
        else if (type == "xy")
        {
            auto* x = host.parameterFor(spec.getProperty("x_param", {}).toString());
            auto* y = host.parameterFor(spec.getProperty("y_param", {}).toString());
            if (x != nullptr && y != nullptr)
                widget->component = std::make_unique<XYPad>(*x, *y, colour);
        }
        else if (parameter != nullptr && type == "toggle")
        {
            widget->component = std::make_unique<Toggle>(*parameter, caption, colour);
        }
        else if (parameter != nullptr && type == "choice")
        {
            widget->component = std::make_unique<Choice>(*parameter, choicesFor(info, *parameter));
        }
        else if (parameter != nullptr)
        {
            const auto style = type == "slider" ? juce::Slider::LinearVertical : (type == "hslider" ? juce::Slider::LinearHorizontal : juce::Slider::RotaryVerticalDrag);
            auto slider = std::make_unique<juce::Slider>(style, juce::Slider::TextBoxBelow);
            slider->setTextBoxStyle(bool(spec.getProperty("value", true)) ? juce::Slider::TextBoxBelow : juce::Slider::NoTextBox, true, bounds.getWidth(), 16);
            slider->setColour(juce::Slider::rotarySliderFillColourId, colour);
            slider->setColour(juce::Slider::trackColourId, colour);
            slider->setColour(juce::Slider::textBoxTextColourId, colour);
            slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            widget->sliderAttachment = std::make_unique<juce::SliderParameterAttachment>(*parameter, *slider);
            slider->setMouseDragSensitivity(180);
            slider->setDoubleClickReturnValue(true, double(parameter->convertFrom0to1(parameter->getDefaultValue())));
            slider->textFromValueFunction = [unit = unitFor(info, *parameter)](double value)
            {
                const auto magnitude = std::abs(value);
                return juce::String(value, magnitude >= 100.0 ? 0 : (magnitude >= 10.0 ? 1 : 2)) + (unit.isNotEmpty() ? " " + unit : juce::String());
            };
            slider->updateText();
            widget->component = std::move(slider);
            if (caption.isNotEmpty() && type != "hslider")
            {
                widget->label = std::make_unique<juce::Label>(juce::String(), caption);
                widget->label->setJustificationType(juce::Justification::centred);
                widget->label->setFont(juce::FontOptions(12.0f, juce::Font::bold));
                widget->label->setColour(juce::Label::textColourId, juce::Colour(0xff8f9a9f));
                widget->label->setBounds(area.removeFromTop(16));
                addAndMakeVisible(*widget->label);
            }
        }
        if (widget->component == nullptr)
            continue;
        widget->component->setBounds(area);
        addAndMakeVisible(*widget->component);
        widgets.push_back(std::move(widget));
    }
}

void LuaPluginView::refreshTables()
{
    lua_newtable(state);
    for (const auto& parameter : info.parameters)
        if (auto* target = host.parameterFor(parameter.id))
        {
            lua_pushnumber(state, target->convertFrom0to1(target->getValue()));
            lua_setfield(state, -2, parameter.id.toRawUTF8());
        }
    lua_setglobal(state, "params");
    lua_newtable(state);
    for (int i = 0; i < info.meters.size(); ++i)
    {
        lua_pushnumber(state, host.meterValue(i));
        lua_setfield(state, -2, info.meters[i].toRawUTF8());
    }
    lua_setglobal(state, "meters");
}

bool LuaPluginView::callLua(const char* function, std::initializer_list<double> arguments)
{
    if (error.isNotEmpty())
        return false;
    armLuaWatchdog(state, 250);
    lua_getglobal(state, function);
    if (!lua_isfunction(state, -1))
    {
        lua_pop(state, 1);
        return false;
    }
    refreshTables();
    for (const auto argument : arguments)
        lua_pushnumber(state, argument);
    if (lua_pcall(state, int(arguments.size()), 0, 0) != LUA_OK)
    {
        error = juce::String::fromUTF8(lua_tostring(state, -1));
        lua_pop(state, 1);
        return false;
    }
    return true;
}

void LuaPluginView::paint(juce::Graphics& g)
{
    g.fillAll(background);
    if (hasDraw)
    {
        juce::Graphics::ScopedSaveState saved(g);
        const GfxScope scope(state, g, accent);
        callLua("draw", { double(getWidth()), double(getHeight()) });
    }
    if (error.isNotEmpty())
    {
        g.setColour(juce::Colour(0xffe8483b));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawFittedText("Lua UI error: " + error, getLocalBounds().removeFromBottom(36).reduced(8, 2), juce::Justification::centredLeft, 2);
    }
}

void LuaPluginView::timerCallback()
{
    if (hasDraw)
        repaint();
    for (auto& widget : widgets)
        if (dynamic_cast<Meter*>(widget->component.get()) != nullptr)
            widget->component->repaint();
}

void LuaPluginView::mouseDown(const juce::MouseEvent& e)
{
    if (callLua("mouse_down", { e.position.x, e.position.y }))
        repaint();
}

void LuaPluginView::mouseDrag(const juce::MouseEvent& e)
{
    if (callLua("mouse_drag", { e.position.x, e.position.y }))
        repaint();
}

void LuaPluginView::mouseUp(const juce::MouseEvent& e)
{
    if (callLua("mouse_up", { e.position.x, e.position.y }))
        repaint();
}

void LuaPluginView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (callLua("mouse_wheel", { e.position.x, e.position.y, wheel.deltaY }))
        repaint();
}

int LuaPluginView::setParam(lua_State* state)
{
    auto* view = from(state);
    auto* parameter = view->host.parameterFor(juce::String::fromUTF8(luaL_checkstring(state, 1)));
    if (parameter == nullptr)
        return luaL_error(state, "unknown parameter %s", lua_tostring(state, 1));
    const auto& range = parameter->getNormalisableRange();
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(juce::jlimit(range.start, range.end, float(luaL_checknumber(state, 2)))));
    parameter->endChangeGesture();
    return 0;
}

int LuaPluginView::getParam(lua_State* state)
{
    auto* parameter = from(state)->host.parameterFor(juce::String::fromUTF8(luaL_checkstring(state, 1)));
    lua_pushnumber(state, parameter != nullptr ? parameter->convertFrom0to1(parameter->getValue()) : 0.0);
    return 1;
}
}
