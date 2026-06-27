#include "element_dsl.hpp"

#include <cstdio>
#include <exception>
#include <string>

using namespace reactcpp::ui;

std::string ArchitectureDrawioXml() {
    return R"drawio(
<mxGraphModel>
  <root>
    <mxCell id="0"/>
    <mxCell id="1" parent="0"/>
    <mxCell id="app" value="App Render" style="rounded=1;whiteSpace=wrap;html=1;fillColor=#DBEAFE;strokeColor=#2563EB;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="40" y="70" width="130" height="60" as="geometry"/>
    </mxCell>
    <mxCell id="reconcile" value="Reconciler" style="rounded=1;whiteSpace=wrap;html=1;fillColor=#DCFCE7;strokeColor=#16A34A;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="230" y="70" width="130" height="60" as="geometry"/>
    </mxCell>
    <mxCell id="layout" value="Yoga Layout" style="rounded=1;whiteSpace=wrap;html=1;fillColor=#FEF3C7;strokeColor=#D97706;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="420" y="70" width="130" height="60" as="geometry"/>
    </mxCell>
    <mxCell id="skia" value="Skia Cache" style="rounded=1;whiteSpace=wrap;html=1;fillColor=#FCE7F3;strokeColor=#DB2777;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="610" y="70" width="130" height="60" as="geometry"/>
    </mxCell>
    <mxCell id="present" value="SDL Window" style="ellipse;whiteSpace=wrap;html=1;fillColor=#E0E7FF;strokeColor=#4F46E5;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="790" y="70" width="120" height="60" as="geometry"/>
    </mxCell>
    <mxCell id="state" value="State Hooks" style="rounded=1;whiteSpace=wrap;html=1;fillColor=#F3F4F6;strokeColor=#6B7280;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="230" y="170" width="130" height="52" as="geometry"/>
    </mxCell>
    <mxCell id="events" value="SDL Events" style="rounded=1;whiteSpace=wrap;html=1;fillColor=#F3F4F6;strokeColor=#6B7280;fontColor=#111827;" vertex="1" parent="1">
      <mxGeometry x="40" y="170" width="130" height="52" as="geometry"/>
    </mxCell>
    <mxCell id="e1" style="endArrow=block;html=1;rounded=0;strokeColor=#6B7280;" edge="1" parent="1" source="app" target="reconcile">
      <mxGeometry relative="1" as="geometry"/>
    </mxCell>
    <mxCell id="e2" style="endArrow=block;html=1;rounded=0;strokeColor=#6B7280;" edge="1" parent="1" source="reconcile" target="layout">
      <mxGeometry relative="1" as="geometry"/>
    </mxCell>
    <mxCell id="e3" style="endArrow=block;html=1;rounded=0;strokeColor=#6B7280;" edge="1" parent="1" source="layout" target="skia">
      <mxGeometry relative="1" as="geometry"/>
    </mxCell>
    <mxCell id="e4" style="endArrow=block;html=1;rounded=0;strokeColor=#6B7280;" edge="1" parent="1" source="skia" target="present">
      <mxGeometry relative="1" as="geometry"/>
    </mxCell>
    <mxCell id="e5" style="endArrow=block;html=1;rounded=0;strokeColor=#6B7280;" edge="1" parent="1" source="events" target="state">
      <mxGeometry relative="1" as="geometry"/>
    </mxCell>
    <mxCell id="e6" style="endArrow=block;html=1;rounded=0;strokeColor=#6B7280;" edge="1" parent="1" source="state" target="reconcile">
      <mxGeometry relative="1" as="geometry"/>
    </mxCell>
  </root>
</mxGraphModel>
)drawio";
}

Element AppRoot() {
    auto counter = use_state<int>(0);

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
            .value("Draw.io architecture canvas:")
            .text_size(18.0f),

        canvas()
            .margin(6.0f)
            .size(720.0f, 230.0f)
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
