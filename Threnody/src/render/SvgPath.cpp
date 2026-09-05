#include "render/SvgPath.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>

namespace threnody::render {
namespace {

class Parser {
public:
    Parser(std::string_view data, ID2D1GeometrySink& sink) : m_data(data), m_sink(sink) {}

    Result<void> run() {
        char command = 0;
        while (true) {
            skipSeparators();
            if (m_pos >= m_data.size()) {
                break;
            }
            const char c = m_data[m_pos];
            if (std::isalpha(static_cast<unsigned char>(c))) {
                command = c;
                ++m_pos;
            } else if (command == 0) {
                return fail("path data must start with a command");
            } else if (command == 'M') {
                command = 'L';  // Implicit repetition of moveto continues as lineto.
            } else if (command == 'm') {
                command = 'l';
            }
            if (const Result<void> step = execute(command); !step) {
                return step;
            }
        }
        if (m_figureOpen) {
            m_sink.EndFigure(D2D1_FIGURE_END_OPEN);
            m_figureOpen = false;
        }
        return {};
    }

private:
    Result<void> fail(const char* what) const {
        return Error::fromHResult(E_INVALIDARG, std::string("SVG path: ") + what + " at offset " + std::to_string(m_pos));
    }

    void skipSeparators() {
        while (m_pos < m_data.size() && (std::isspace(static_cast<unsigned char>(m_data[m_pos])) || m_data[m_pos] == ',')) {
            ++m_pos;
        }
    }

    std::optional<float> number() {
        skipSeparators();
        const std::size_t start = m_pos;
        std::size_t end = start;
        if (end < m_data.size() && (m_data[end] == '+' || m_data[end] == '-')) {
            ++end;
        }
        bool digits = false;
        while (end < m_data.size() && std::isdigit(static_cast<unsigned char>(m_data[end]))) {
            ++end;
            digits = true;
        }
        if (end < m_data.size() && m_data[end] == '.') {
            ++end;
            while (end < m_data.size() && std::isdigit(static_cast<unsigned char>(m_data[end]))) {
                ++end;
                digits = true;
            }
        }
        if (!digits) {
            return std::nullopt;
        }
        if (end < m_data.size() && (m_data[end] == 'e' || m_data[end] == 'E')) {
            std::size_t exp = end + 1;
            if (exp < m_data.size() && (m_data[exp] == '+' || m_data[exp] == '-')) {
                ++exp;
            }
            if (exp < m_data.size() && std::isdigit(static_cast<unsigned char>(m_data[exp]))) {
                while (exp < m_data.size() && std::isdigit(static_cast<unsigned char>(m_data[exp]))) {
                    ++exp;
                }
                end = exp;
            }
        }
        float value = 0.0f;
        const std::string text{m_data.substr(start, end - start)};
        value = std::strtof(text.c_str(), nullptr);
        m_pos = end;
        return value;
    }

    // Arc flags are single characters and may be run together ("01").
    std::optional<bool> flag() {
        skipSeparators();
        if (m_pos < m_data.size() && (m_data[m_pos] == '0' || m_data[m_pos] == '1')) {
            return m_data[m_pos++] == '1';
        }
        return std::nullopt;
    }

    void ensureFigure() {
        if (!m_figureOpen) {
            m_sink.BeginFigure(m_current, D2D1_FIGURE_BEGIN_FILLED);
            m_start = m_current;
            m_figureOpen = true;
        }
    }

    D2D1_POINT_2F resolve(bool relative, float x, float y) const noexcept {
        return relative ? D2D1_POINT_2F{m_current.x + x, m_current.y + y} : D2D1_POINT_2F{x, y};
    }

    Result<void> execute(char command) {
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        switch (static_cast<char>(std::toupper(static_cast<unsigned char>(command)))) {
            case 'M': {
                const auto x = number(), y = number();
                if (!x || !y) return fail("moveto needs two numbers");
                if (m_figureOpen) {
                    m_sink.EndFigure(D2D1_FIGURE_END_OPEN);
                    m_figureOpen = false;
                }
                m_current = resolve(relative, *x, *y);
                ensureFigure();
                m_lastControl.reset();
                return {};
            }
            case 'L': {
                const auto x = number(), y = number();
                if (!x || !y) return fail("lineto needs two numbers");
                ensureFigure();
                m_current = resolve(relative, *x, *y);
                m_sink.AddLine(m_current);
                m_lastControl.reset();
                return {};
            }
            case 'H': {
                const auto x = number();
                if (!x) return fail("horizontal lineto needs a number");
                ensureFigure();
                m_current.x = relative ? m_current.x + *x : *x;
                m_sink.AddLine(m_current);
                m_lastControl.reset();
                return {};
            }
            case 'V': {
                const auto y = number();
                if (!y) return fail("vertical lineto needs a number");
                ensureFigure();
                m_current.y = relative ? m_current.y + *y : *y;
                m_sink.AddLine(m_current);
                m_lastControl.reset();
                return {};
            }
            case 'C': {
                const auto x1 = number(), y1 = number(), x2 = number(), y2 = number(), x = number(), y = number();
                if (!x1 || !y1 || !x2 || !y2 || !x || !y) return fail("curveto needs six numbers");
                ensureFigure();
                const D2D1_BEZIER_SEGMENT segment{resolve(relative, *x1, *y1), resolve(relative, *x2, *y2),
                                                  resolve(relative, *x, *y)};
                m_sink.AddBezier(segment);
                m_lastControl = segment.point2;
                m_current = segment.point3;
                return {};
            }
            case 'S': {
                const auto x2 = number(), y2 = number(), x = number(), y = number();
                if (!x2 || !y2 || !x || !y) return fail("smooth curveto needs four numbers");
                ensureFigure();
                const D2D1_POINT_2F first =
                    m_lastControl ? D2D1_POINT_2F{2.0f * m_current.x - m_lastControl->x, 2.0f * m_current.y - m_lastControl->y}
                                  : m_current;
                const D2D1_BEZIER_SEGMENT segment{first, resolve(relative, *x2, *y2), resolve(relative, *x, *y)};
                m_sink.AddBezier(segment);
                m_lastControl = segment.point2;
                m_current = segment.point3;
                return {};
            }
            case 'Q': {
                const auto x1 = number(), y1 = number(), x = number(), y = number();
                if (!x1 || !y1 || !x || !y) return fail("quadratic curveto needs four numbers");
                ensureFigure();
                const D2D1_QUADRATIC_BEZIER_SEGMENT segment{resolve(relative, *x1, *y1), resolve(relative, *x, *y)};
                m_sink.AddQuadraticBezier(segment);
                m_lastControl = segment.point1;
                m_current = segment.point2;
                return {};
            }
            case 'A': {
                const auto rx = number(), ry = number(), rotation = number();
                const auto large = flag(), sweep = flag();
                const auto x = number(), y = number();
                if (!rx || !ry || !rotation || !large || !sweep || !x || !y) return fail("arc needs seven values");
                ensureFigure();
                const D2D1_POINT_2F end = resolve(relative, *x, *y);
                if (*rx == 0.0f || *ry == 0.0f) {
                    m_sink.AddLine(end);
                } else if (end.x != m_current.x || end.y != m_current.y) {
                    const D2D1_ARC_SEGMENT arc{
                        .point = end,
                        .size = {std::fabs(*rx), std::fabs(*ry)},
                        .rotationAngle = *rotation,
                        .sweepDirection = *sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                        .arcSize = *large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL,
                    };
                    m_sink.AddArc(arc);
                }
                m_current = end;
                m_lastControl.reset();
                return {};
            }
            case 'Z': {
                if (m_figureOpen) {
                    m_sink.EndFigure(D2D1_FIGURE_END_CLOSED);
                    m_figureOpen = false;
                }
                m_current = m_start;
                m_lastControl.reset();
                return {};
            }
            default:
                return fail("unknown command");
        }
    }

    std::string_view m_data;
    ID2D1GeometrySink& m_sink;
    std::size_t m_pos{};
    D2D1_POINT_2F m_current{};
    D2D1_POINT_2F m_start{};
    std::optional<D2D1_POINT_2F> m_lastControl;
    bool m_figureOpen{false};
};

}  // namespace

Result<winrt::com_ptr<ID2D1PathGeometry>> pathGeometryFromSvg(ID2D1Factory1& factory, std::string_view pathData,
                                                              D2D1_FILL_MODE fillMode) {
    winrt::com_ptr<ID2D1PathGeometry> geometry;
    HRESULT hr = factory.CreatePathGeometry(geometry.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "CreatePathGeometry(svg)");
    }
    winrt::com_ptr<ID2D1GeometrySink> sink;
    hr = geometry->Open(sink.put());
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "ID2D1PathGeometry::Open(svg)");
    }
    sink->SetFillMode(fillMode);

    Parser parser{pathData, *sink};
    const Result<void> parsed = parser.run();
    hr = sink->Close();
    if (!parsed) {
        return parsed.error();
    }
    if (FAILED(hr)) {
        return Error::fromHResult(hr, "ID2D1GeometrySink::Close(svg)");
    }
    return geometry;
}

}  // namespace threnody::render
