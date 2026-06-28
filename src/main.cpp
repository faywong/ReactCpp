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
            .size(820.0f, 330.0f)
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
