#include "element_dsl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#include <atomic>
#include <chrono>
#include <string>

using namespace reactcpp::ui;

namespace {

struct LiveChartData {
    std::shared_ptr<reactcpp::VectorDataSource<reactcpp::LinePoint>> line_points =
        std::make_shared<reactcpp::VectorDataSource<reactcpp::LinePoint>>();
    std::shared_ptr<reactcpp::VectorDataSource<reactcpp::LinePoint>> scatter_points =
        std::make_shared<reactcpp::VectorDataSource<reactcpp::LinePoint>>();
    std::shared_ptr<reactcpp::VectorDataSource<reactcpp::LinePoint>> area_points =
        std::make_shared<reactcpp::VectorDataSource<reactcpp::LinePoint>>();
    std::shared_ptr<reactcpp::VectorDataSource<double>> bar_values =
        std::make_shared<reactcpp::VectorDataSource<double>>();
    std::shared_ptr<reactcpp::VectorDataSource<double>> bar_x =
        std::make_shared<reactcpp::VectorDataSource<double>>();
    std::shared_ptr<reactcpp::VectorDataSource<double>> circle_x =
        std::make_shared<reactcpp::VectorDataSource<double>>();
    std::shared_ptr<reactcpp::VectorDataSource<double>> circle_y =
        std::make_shared<reactcpp::VectorDataSource<double>>();
    std::shared_ptr<reactcpp::VectorDataSource<double>> circle_r =
        std::make_shared<reactcpp::VectorDataSource<double>>();
    std::shared_ptr<reactcpp::VectorDataSource<double>> histogram_samples =
        std::make_shared<reactcpp::VectorDataSource<double>>();
    std::shared_ptr<reactcpp::RiveInputs> process_animation =
        std::make_shared<reactcpp::RiveInputs>();

    std::atomic<bool> started{false};
    std::jthread worker;

    void start() {
        bool expected = false;
        if (!started.compare_exchange_strong(expected, true)) {
            return;
        }

        line_points->set_window_size(240);
        line_points->set_time_window(60.0);
        scatter_points->set_window_size(240);
        scatter_points->set_time_window(60.0);
        area_points->set_window_size(240);
        area_points->set_time_window(60.0);
        bar_values->set_window_size(120);
        bar_x->set_window_size(120);
        circle_x->set_window_size(200);
        circle_y->set_window_size(200);
        circle_r->set_window_size(200);
        histogram_samples->set_window_size(1200);

        worker = std::jthread([this](std::stop_token stop) {
            std::mt19937 rng(20260628);
            std::normal_distribution<double> noise(0.0, 1.0);
            std::size_t tick = 0;

            while (!stop.stop_requested()) {
                const double t = static_cast<double>(tick) * 0.08;
                const double mean = std::sin(t * 0.12) * 2.0;
                std::normal_distribution<double> hist_noise(mean, 4.5);
                const double x = static_cast<double>(tick) * 0.25;
                const double y = std::sin(x * 0.35 + t) * 18.0 + std::sin(x * 0.11 + t * 0.6) * 4.0;
                line_points->push_back({x, y});
                scatter_points->push_back({x, y + std::sin(x * 1.1 - t) + 8.0 + noise(rng) * 0.8});
                area_points->push_back({x, y * 0.55 + 18.0});

                bar_x->push_back(x);
                bar_values->push_back(
                    std::sin(x * 0.22 + t * 0.5) * 30.0 + 35.0 + std::cos(t * 0.8 + x * 0.3) * 4.0
                );

                circle_x->push_back((std::fmod(x, 26.0) - 2.0));
                circle_y->push_back(std::cos(x * 0.06 + t) * 8.0 + 18.0);
                circle_r->push_back(2.0 + std::fmod(std::fabs(std::sin(static_cast<double>(tick) * 0.15 + t * 0.7)) * 6.0, 1.0) * 2.0);
                histogram_samples->push_back(hist_noise(rng));

                const double flow_rate = std::fabs(std::sin(t * 0.9)) * 100.0;
                process_animation->set_number("temperature", 68.0 + std::sin(t * 0.33) * 24.0);
                process_animation->set_number("flow_rate", flow_rate);
                process_animation->set_bool("is_error", std::fmod(static_cast<double>(tick), 97.0) > 88.0);
                process_animation->set_time_scale(std::max(0.1, flow_rate / 100.0));

                ++tick;
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
        });
    }
};

LiveChartData& live_chart_data() {
    static LiveChartData data;
    return data;
}

} // namespace

std::string ArchitectureDrawioXml() {
    return R"drawio(
<mxGraphModel>
  <root>
    <mxCell id="0"/>
    <mxCell id="1" parent="0"/>
    <mxCell id="title" value="ReactCpp draw.io renderer showcase&lt;br&gt;raw mxGraphModel -> Skia" style="swimlane;rounded=1;whiteSpace=wrap;html=1;startSize=34;fontSize=15;fillColor=#F8FAFC;strokeColor=#64748B;fontColor=#0F172A;" vertex="1" parent="1">
      <mxGeometry x="20" y="20" width="900" height="340" as="geometry"/>
    </mxCell>
    <mxCell id="input" value="SDL Events&lt;br&gt;mouse / IME / wheel" style="rounded=1;whiteSpace=wrap;html=1;fontSize=13;fillColor=#DBEAFE;strokeColor=#2563EB;strokeWidth=2;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="60" y="82" width="132" height="64" as="geometry"/>
    </mxCell>
    <mxCell id="render" value="App Render&lt;br&gt;components + hooks" style="rounded=1;whiteSpace=wrap;html=1;fontSize=13;fillColor=#E0F2FE;strokeColor=#0284C7;strokeWidth=2;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="244" y="82" width="140" height="64" as="geometry"/>
    </mxCell>
    <mxCell id="reconcile" value="Reconciler&lt;br&gt;diff + dirty flags" style="shape=rhombus;whiteSpace=wrap;html=1;fontSize=13;fillColor=#DCFCE7;strokeColor=#16A34A;strokeWidth=2;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="452" y="68" width="116" height="92" as="geometry"/>
    </mxCell>
    <mxCell id="layout" value="Yoga Layout&lt;br&gt;intrinsic text sizing" style="rounded=1;whiteSpace=wrap;html=1;fontSize=13;fillColor=#FEF3C7;strokeColor=#D97706;strokeWidth=2;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="632" y="82" width="152" height="64" as="geometry"/>
    </mxCell>
    <mxCell id="present" value="SDL Window" style="ellipse;whiteSpace=wrap;html=1;fontSize=13;fillColor=#E0E7FF;strokeColor=#4F46E5;strokeWidth=2;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="812" y="84" width="84" height="60" as="geometry"/>
    </mxCell>
    <mxCell id="state" value="Hook state store&lt;br&gt;batched updates" style="shape=cylinder;whiteSpace=wrap;html=1;fontSize=13;fillColor=#F3E8FF;strokeColor=#9333EA;strokeWidth=2;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="238" y="220" width="142" height="72" as="geometry"/>
    </mxCell>
    <mxCell id="picture" value="SkPicture cache&lt;br&gt;retained frame graph" style="rounded=1;whiteSpace=wrap;html=1;fontSize=13;fillColor=#FCE7F3;strokeColor=#DB2777;strokeWidth=2;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="634" y="220" width="148" height="64" as="geometry"/>
    </mxCell>
    <mxCell id="clipboard" value="Context menu&lt;br&gt;Copy to clipboard" style="shape=image;whiteSpace=wrap;html=1;fontSize=13;fillColor=#F1F5F9;strokeColor=#475569;dashed=1;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="60" y="224" width="132" height="60" as="geometry"/>
    </mxCell>
    <mxCell id="e1" style="endArrow=block;html=1;rounded=0;strokeColor=#475569;strokeWidth=2;" edge="1" parent="1" source="input" target="render">
      <mxGeometry relative="1" as="geometry"/>
    </mxCell>
    <mxCell id="e2" style="endArrow=block;html=1;rounded=0;strokeColor=#475569;strokeWidth=2;" edge="1" parent="1" source="render" target="reconcile">
      <mxGeometry relative="1" as="geometry"/>
    </mxCell>
    <mxCell id="e3" style="endArrow=block;html=1;rounded=0;strokeColor=#475569;strokeWidth=2;" edge="1" parent="1" source="reconcile" target="layout">
      <mxGeometry relative="1" as="geometry"/>
    </mxCell>
    <mxCell id="e4" style="endArrow=block;html=1;rounded=0;strokeColor=#475569;strokeWidth=2;" edge="1" parent="1" source="layout" target="present">
      <mxGeometry relative="1" as="geometry"/>
    </mxCell>
    <mxCell id="e5" value="setState()" style="endArrow=block;html=1;rounded=0;strokeColor=#9333EA;strokeWidth=2;dashed=1;fontColor=#6B21A8;fontSize=12;" edge="1" parent="1" source="input" target="state">
      <mxGeometry relative="1" as="geometry">
        <mxPoint x="126" y="190" as="sourcePoint"/>
        <mxPoint x="310" y="190" as="targetPoint"/>
        <Array as="points">
          <mxPoint x="126" y="190"/>
          <mxPoint x="310" y="190"/>
        </Array>
      </mxGeometry>
    </mxCell>
    <mxCell id="e6" value="next frame" style="endArrow=block;html=1;rounded=0;strokeColor=#9333EA;strokeWidth=2;dashed=1;fontColor=#6B21A8;fontSize=12;" edge="1" parent="1" source="state" target="reconcile">
      <mxGeometry relative="1" as="geometry">
        <Array as="points">
          <mxPoint x="520" y="256"/>
        </Array>
      </mxGeometry>
    </mxCell>
    <mxCell id="e7" value="record dirty nodes" style="endArrow=block;html=1;rounded=0;strokeColor=#DB2777;strokeWidth=2;fontColor=#9D174D;fontSize=12;" edge="1" parent="1" source="layout" target="picture">
      <mxGeometry relative="1" as="geometry">
        <Array as="points">
          <mxPoint x="708" y="184"/>
        </Array>
      </mxGeometry>
    </mxCell>
    <mxCell id="e8" value="draw cached pictures" style="endArrow=block;html=1;rounded=0;strokeColor=#DB2777;strokeWidth=2;fontColor=#9D174D;fontSize=12;" edge="1" parent="1" source="picture" target="present">
      <mxGeometry relative="1" as="geometry">
        <Array as="points">
          <mxPoint x="854" y="252"/>
        </Array>
      </mxGeometry>
    </mxCell>
    <mxCell id="e9" value="right click" style="endArrow=block;html=1;rounded=0;strokeColor=#475569;strokeWidth=2;dashed=1;fontColor=#334155;fontSize=12;" edge="1" parent="1" source="input" target="clipboard">
      <mxGeometry relative="1" as="geometry"/>
    </mxCell>
    <mxCell id="legend" value="Rendered features: swimlane, rounded rectangles, ellipse, rhombus, cylinder, image placeholder, wrapped labels, dashed strokes, waypoint polylines, arrowheads" style="rounded=1;whiteSpace=wrap;html=1;fontSize=12;align=left;fillColor=#FFFFFF;strokeColor=#CBD5E1;fontColor=#334155;" vertex="1" parent="1">
      <mxGeometry x="60" y="310" width="800" height="34" as="geometry"/>
    </mxCell>
  </root>
</mxGraphModel>
)drawio";
}

Element AppRoot() {
    auto counter = use_state<int>(0);
    live_chart_data().start();

    auto& charts = live_chart_data();

    return view()
        .column()
        .justify(JustifyContent::FlexStart)
        .align(AlignItems::Stretch)
        .padding(24.0f)
        .bg(0.95f, 0.96f, 0.98f)
    (
        text()
            .margin(6.0f)
            .value(std::string("ReactCpp GUI, clicked for ") + std::to_string(counter.get()) + " times")
            .text_size(30.0f)
            .text_color(0.12f, 0.15f, 0.24f),

        button()
            .margin(6.0f)
            .size(260.0f, 48.0f)
            .bg(0.25f, 0.52f, 0.93f)
            .label("Test Button")
            .text_size(20.0f)
            .text_color(1.0f, 1.0f, 1.0f)
            .on_click([counter]() {
                counter.update([](int v) {
                    return v + 1;
                });
            }),

        text()
            .margin(6.0f)
            .value("Editable Input:")
            .text_size(18.0f),

        input()
            .margin(6.0f)
            .size(380.0f, 44.0f)
            .bg(1.0f, 1.0f, 1.0f)
            .placeholder("Click to focus, type text, backspace works")
            .value("")

        ,
        text()
            .margin(6.0f)
            .value("Editable InputArea (wrap + scroll + Enter newline):")
            .text_size(18.0f)

        ,
        input_area()
            .margin(6.0f)
            .size(520.0f, 180.0f)
            .bg(1.0f, 1.0f, 1.0f)
            .border_color(0.70f, 0.70f, 0.70f)
            .text_size(16.0f)
            .text_color(0.10f, 0.10f, 0.10f)
            .placeholder("Click to focus; Enter inserts newline; mouse drag selects; wheel scrolls")
            .value(
                "A very long line to test wrapping: The_quick_brown_fox_jumps_over_the_lazy_dog_0123456789_" \
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz\n"
                "Line 2: 中文换行测试：这是一段很长很长的中文文本，用来验证在超过宽度之后是否会自动换行，并且光标、选区依然正确。\n"
                "Line 3: Try selecting across wrapped lines, then press Backspace.\n"
                "Line 4: Scroll with mouse wheel when content exceeds the view height."
            ),

        text()
            .margin(6.0f)
            .value("Charts powered by VectorDataSource + request_repaint (live)")
            .text_size(18.0f),

        rive_player()
            .margin(6.0f)
            .size(900.0f, 170.0f)
            .bg(1.0f, 1.0f, 1.0f)
            .source("assets/hmi/boiler_process.riv")
            .artboard("BoilerProcess")
            .state_machine("SCADA")
            .inputs(charts.process_animation),

        line_chart()
            .margin(6.0f)
            .size(900.0f, 240.0f)
            .title("Streaming Line (windowed)")
            .xlabel("time")
            .ylabel("value")
            .theme(ChartTheme::Seaborn)
            .tick_count(8)
            .show_grid(true)
            .grid(ChartGrid::Both)
            .bg(1.0f, 1.0f, 1.0f)
            .source(charts.line_points)
            .line_color(0.11f, 0.43f, 0.82f)
            .marker_color(0.11f, 0.33f, 0.55f)
            .line_width(2.0f)
            .show_markers(false)
            .fill_area(true),

        scatter_chart()
            .margin(6.0f)
            .size(900.0f, 240.0f)
            .title("Streaming Scatter (windowed)")
            .xlabel("time")
            .ylabel("value")
            .theme(ChartTheme::SolarizedDark)
            .tick_count(7)
            .grid(ChartGrid::Both)
            .axis_width(1.1f)
            .bg(0.99f, 0.99f, 1.0f)
            .source(charts.scatter_points)
            .line_color(0.0f, 0.0f, 0.0f)
            .point_color(0.81f, 0.14f, 0.24f)
            .marker_size(3.0f),

        area_chart()
            .margin(6.0f)
            .size(900.0f, 240.0f)
            .title("Streaming Area")
            .xlabel("time")
            .ylabel("smoothed value")
            .theme(ChartTheme::SolarizedLight)
            .tick_count(6)
            .bg(1.0f, 1.0f, 1.0f)
            .source(charts.area_points)
            .fill(0.17f, 0.63f, 0.53f, 0.22f)
            .line_color(0.11f, 0.43f, 0.82f)
            .line_width(2.0f),

        bar_chart()
            .margin(6.0f)
            .size(900.0f, 250.0f)
            .title("Sliding Bars")
            .xlabel("index")
            .ylabel("amplitude")
            .theme(ChartTheme::Monochrome)
            .tick_count(5)
            .bg(1.0f, 1.0f, 1.0f)
            .values_source(charts.bar_values)
            .x_source(charts.bar_x)
            .color(0.23f, 0.5f, 0.84f, 1.0f),

        circle_chart()
            .margin(6.0f)
            .size(900.0f, 260.0f)
            .title("Streaming Bubble")
            .xlabel("time")
            .ylabel("value")
            .theme(ChartTheme::HighContrast)
            .tick_count(6)
            .bg(1.0f, 1.0f, 1.0f)
            .x_source(charts.circle_x)
            .y_source(charts.circle_y)
            .radius_source(charts.circle_r)
            .fill_color(0.83f, 0.16f, 0.2f, 0.86f),

        histogram_chart()
            .margin(6.0f)
            .size(900.0f, 220.0f)
            .title("Streaming Histogram")
            .xlabel("sample")
            .ylabel("frequency density")
            .theme(ChartTheme::Matplotlib)
            .tick_count(7)
            .bg(1.0f, 1.0f, 1.0f)
            .source(charts.histogram_samples)
            .bin_count(24)
            .normalization(HistogramNormalization::CountDensity)
            .color(0.26f, 0.76f, 0.89f),

        text()
            .margin(6.0f)
            .value("Draw.io architecture canvas:")
            .text_size(18.0f),

        canvas()
            .margin(6.0f)
            .size(900.0f, 330.0f)
            .bg(1.0f, 1.0f, 1.0f)
            .diagram_padding(18.0f)
            .drawio_xml(ArchitectureDrawioXml())
    );
}

int main() {
    try {
        return run_react_app([]() {
            return AppRoot();
        });
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
}
