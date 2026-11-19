

// ============================================================================
//  main.cpp —— 基于 wxWidgets 的电路编辑器
// ============================================================================

#pragma execution_character_set("utf-8")

#include <wx/wx.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/toolbar.h>
#include <wx/menu.h>
#include <wx/artprov.h>
#include <wx/statline.h>
#include <wx/dcbuffer.h>
#include <wx/choicdlg.h>
#include <wx/textdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <vector>
#include <memory>
#include <wx/dcmemory.h>
#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <utility>


static void DrawBezierPath(wxDC& dc,
    const wxPoint& p0, const wxPoint& c1,
    const wxPoint& c2, const wxPoint& p1,
    int steps = 20)
{
    std::vector<wxPoint> pts;
    pts.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i)
    {
        double t = (double)i / (double)steps;
        double mt = 1.0 - t;
        double x = mt * mt * mt * p0.x + 3 * mt * mt * t * c1.x + 3 * mt * t * t * c2.x + t * t * t * p1.x;
        double y = mt * mt * mt * p0.y + 3 * mt * mt * t * c1.y + 3 * mt * t * t * c2.y + t * t * t * p1.y;
        pts.push_back(wxPoint((int)(x + (x >= 0 ? 0.5 : -0.5)),
            (int)(y + (y >= 0 ? 0.5 : -0.5))));
    }
    dc.DrawLines((int)pts.size(), pts.data());
}

static int GetNetVal(const std::map<std::pair<int, int>, int>& netValues, const wxPoint& p)
{
    auto it = netValues.find({ p.x, p.y });
    return (it != netValues.end()) ? it->second : 0;
}


class Component
{
public:
    wxPoint pos;
    bool selected = false;
    int value = 0;

    explicit Component(const wxPoint& p) : pos(p) {}
    virtual ~Component() {}

    virtual wxString GetType() const = 0;
    virtual void Draw(wxDC& dc) const = 0;
    virtual bool HitTest(const wxPoint& p) const = 0;
    virtual std::vector<wxPoint> GetPins() const { return {}; }
    virtual wxRect GetBounds() const { return wxRect(pos.x - 20, pos.y - 20, 40, 40); }

    virtual void ReadInputs(const std::map<std::pair<int, int>, int>& /*netValues*/) {}
    virtual void WriteOutputs(std::map<std::pair<int, int>, int>& /*netValues*/) const {}
};


class PinComponent : public Component
{
public:
    int userValue = 0;

    explicit PinComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("Pin"); }

    void ToggleValue() { userValue = 1 - userValue; }

    // 接线点放在 Pin 的中心（单一接线点）
    std::vector<wxPoint> GetPins() const override
    {
        return { { pos.x, pos.y } };
    }

    void ReadInputs(const std::map<std::pair<int, int>, int>& netValues) override
    {
        value = userValue;
        if (!value)
        {
            std::vector<wxPoint> pins = GetPins();
            for (size_t i = 0; i < pins.size(); ++i)
                if (GetNetVal(netValues, pins[i]) == 1) { value = 1; break; }
        }
    }

    void WriteOutputs(std::map<std::pair<int, int>, int>& netValues) const override
    {
        if (userValue)
        {
            std::vector<wxPoint> pins = GetPins();
            for (size_t i = 0; i < pins.size(); ++i)
                netValues[{pins[i].x, pins[i].y}] = 1;
        }
    }

    void Draw(wxDC& dc) const override
    {
        wxColour col = value ? wxColour(0, 220, 0) : *wxBLACK;
        dc.SetPen(wxPen(col, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int half = 10;
        dc.DrawRectangle(pos.x - half, pos.y - half, half * 2, half * 2);
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(col));
        dc.DrawCircle(pos.x, pos.y, 6);
    }

    bool HitTest(const wxPoint& p) const override
    {
        return p.x >= pos.x - 12 && p.x <= pos.x + 12
            && p.y >= pos.y - 12 && p.y <= pos.y + 12;
    }
};


class NotGateComponent : public Component
{
    int inVal = 0;
public:
    explicit NotGateComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("NOT Gate"); }
    std::vector<wxPoint> GetPins() const override
    {
        return { {pos.x - 20, pos.y}, {pos.x + 30, pos.y} };
    }
    void ReadInputs(const std::map<std::pair<int, int>, int>& nets) override
    {
        auto pins = GetPins();
        inVal = GetNetVal(nets, pins[0]);
        value = (inVal == 0) ? 1 : 0;
    }
    void WriteOutputs(std::map<std::pair<int, int>, int>& nets) const override
    {
        if (value) { auto pins = GetPins(); nets[{pins[1].x, pins[1].y}] = 1; }
    }
    void Draw(wxDC& dc) const override
    {
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = pos.x, y = pos.y;
        wxPoint tri[3] = {
            wxPoint(x - 20, y - 20),
            wxPoint(x - 20, y + 20),
            wxPoint(x + 20, y),
        };
        dc.DrawPolygon(3, tri);
        dc.DrawCircle(x + 25, y, 5);
    }
    bool HitTest(const wxPoint& p) const override
    {
        wxRect r(pos.x - 20, pos.y - 20, 50, 40);
        r.Inflate(2);
        return r.Contains(p);
    }
};


class BufferComponent : public Component
{
    int inVal = 0;
public:
    explicit BufferComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("Buffer"); }
    std::vector<wxPoint> GetPins() const override
    {
        return { {pos.x - 20, pos.y}, {pos.x + 20, pos.y} };
    }
    void ReadInputs(const std::map<std::pair<int, int>, int>& nets) override
    {
        auto pins = GetPins();
        inVal = GetNetVal(nets, pins[0]);
        value = inVal;
    }
    void WriteOutputs(std::map<std::pair<int, int>, int>& nets) const override
    {
        if (value) { auto pins = GetPins(); nets[{pins[1].x, pins[1].y}] = 1; }
    }
    void Draw(wxDC& dc) const override
    {
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = pos.x, y = pos.y;
        wxPoint tri[3] = {
            wxPoint(x - 20, y - 20),
            wxPoint(x - 20, y + 20),
            wxPoint(x + 20, y),
        };
        dc.DrawPolygon(3, tri);
    }
    bool HitTest(const wxPoint& p) const override
    {
        wxRect r(pos.x - 20, pos.y - 20, 40, 40);
        r.Inflate(2);
        return r.Contains(p);
    }
};


class AndGateComponent : public Component
{
    int in0 = 0, in1 = 0;
public:
    explicit AndGateComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("AND Gate"); }
    std::vector<wxPoint> GetPins() const override
    {
        return { {pos.x - 20, pos.y - 10}, {pos.x - 20, pos.y + 10}, {pos.x + 20, pos.y} };
    }
    void ReadInputs(const std::map<std::pair<int, int>, int>& nets) override
    {
        auto pins = GetPins();
        in0 = GetNetVal(nets, pins[0]);
        in1 = GetNetVal(nets, pins[1]);
        value = (in0 && in1) ? 1 : 0;
    }
    void WriteOutputs(std::map<std::pair<int, int>, int>& nets) const override
    {
        if (value) { auto pins = GetPins(); nets[{pins[2].x, pins[2].y}] = 1; }
    }
    void Draw(wxDC& dc) const override
    {
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = pos.x, y = pos.y;
        dc.DrawLine(x - 20, y - 20, x - 20, y + 20);
        dc.DrawLine(x - 20, y - 20, x, y - 20);
        dc.DrawLine(x - 20, y + 20, x, y + 20);
        dc.DrawEllipticArc(x - 20, y - 20, 40, 40, 270, 90);
    }
    bool HitTest(const wxPoint& p) const override
    {
        wxRect r(pos.x - 20, pos.y - 20, 40, 40);
        r.Inflate(2);
        return r.Contains(p);
    }
};


class OrGateComponent : public Component
{
    int in0 = 0, in1 = 0;
public:
    explicit OrGateComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("OR Gate"); }
    std::vector<wxPoint> GetPins() const override
    {
        return { {pos.x - 20, pos.y - 10}, {pos.x - 20, pos.y + 10}, {pos.x + 20, pos.y} };
    }
    void ReadInputs(const std::map<std::pair<int, int>, int>& nets) override
    {
        auto pins = GetPins();
        in0 = GetNetVal(nets, pins[0]);
        in1 = GetNetVal(nets, pins[1]);
        value = (in0 || in1) ? 1 : 0;
    }
    void WriteOutputs(std::map<std::pair<int, int>, int>& nets) const override
    {
        if (value) { auto pins = GetPins(); nets[{pins[2].x, pins[2].y}] = 1; }
    }
    void Draw(wxDC& dc) const override
    {
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = pos.x, y = pos.y;
        DrawBezierPath(dc, wxPoint(x - 20, y - 20), wxPoint(x - 5, y - 20),
            wxPoint(x - 5, y + 20), wxPoint(x - 20, y + 20));
        DrawBezierPath(dc, wxPoint(x - 20, y + 20), wxPoint(x - 5, y + 20),
            wxPoint(x + 10, y + 15), wxPoint(x + 20, y));
        DrawBezierPath(dc, wxPoint(x + 20, y), wxPoint(x + 10, y - 15),
            wxPoint(x - 5, y - 20), wxPoint(x - 20, y - 20));
    }
    bool HitTest(const wxPoint& p) const override
    {
        wxRect r(pos.x - 20, pos.y - 20, 40, 40);
        r.Inflate(2);
        return r.Contains(p);
    }
};


class NandGateComponent : public Component
{
    int in0 = 0, in1 = 0;
public:
    explicit NandGateComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("NAND Gate"); }
    std::vector<wxPoint> GetPins() const override
    {
        return { {pos.x - 20, pos.y - 10}, {pos.x - 20, pos.y + 10}, {pos.x + 30, pos.y} };
    }
    void ReadInputs(const std::map<std::pair<int, int>, int>& nets) override
    {
        auto pins = GetPins();
        in0 = GetNetVal(nets, pins[0]);
        in1 = GetNetVal(nets, pins[1]);
        value = (in0 && in1) ? 0 : 1;
    }
    void WriteOutputs(std::map<std::pair<int, int>, int>& nets) const override
    {
        if (value) { auto pins = GetPins(); nets[{pins[2].x, pins[2].y}] = 1; }
    }
    void Draw(wxDC& dc) const override
    {
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = pos.x, y = pos.y;
        dc.DrawLine(x - 20, y - 20, x - 20, y + 20);
        dc.DrawLine(x - 20, y - 20, x, y - 20);
        dc.DrawLine(x - 20, y + 20, x, y + 20);
        dc.DrawEllipticArc(x - 20, y - 20, 40, 40, 270, 90);
        dc.DrawCircle(x + 25, y, 5);
    }
    bool HitTest(const wxPoint& p) const override
    {
        wxRect r(pos.x - 20, pos.y - 20, 50, 40);
        r.Inflate(2);
        return r.Contains(p);
    }
};


class NorGateComponent : public Component
{
    int in0 = 0, in1 = 0;
public:
    explicit NorGateComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("NOR Gate"); }
    std::vector<wxPoint> GetPins() const override
    {
        return { {pos.x - 20, pos.y - 10}, {pos.x - 20, pos.y + 10}, {pos.x + 30, pos.y} };
    }
    void ReadInputs(const std::map<std::pair<int, int>, int>& nets) override
    {
        auto pins = GetPins();
        in0 = GetNetVal(nets, pins[0]);
        in1 = GetNetVal(nets, pins[1]);
        value = (in0 || in1) ? 0 : 1;
    }
    void WriteOutputs(std::map<std::pair<int, int>, int>& nets) const override
    {
        if (value) { auto pins = GetPins(); nets[{pins[2].x, pins[2].y}] = 1; }
    }
    void Draw(wxDC& dc) const override
    {
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = pos.x, y = pos.y;
        DrawBezierPath(dc, wxPoint(x - 20, y - 20), wxPoint(x - 5, y - 20),
            wxPoint(x - 5, y + 20), wxPoint(x - 20, y + 20));
        DrawBezierPath(dc, wxPoint(x - 20, y + 20), wxPoint(x - 5, y + 20),
            wxPoint(x + 10, y + 15), wxPoint(x + 20, y));
        DrawBezierPath(dc, wxPoint(x + 20, y), wxPoint(x + 10, y - 15),
            wxPoint(x - 5, y - 20), wxPoint(x - 20, y - 20));
        dc.DrawCircle(x + 25, y, 5);
    }
    bool HitTest(const wxPoint& p) const override
    {
        wxRect r(pos.x - 20, pos.y - 20, 50, 40);
        r.Inflate(2);
        return r.Contains(p);
    }
};


class XorGateComponent : public Component
{
    int in0 = 0, in1 = 0;
public:
    explicit XorGateComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("XOR Gate"); }
    std::vector<wxPoint> GetPins() const override
    {
        return { {pos.x - 30, pos.y - 10}, {pos.x - 30, pos.y + 10}, {pos.x + 20, pos.y} };
    }
    void ReadInputs(const std::map<std::pair<int, int>, int>& nets) override
    {
        auto pins = GetPins();
        in0 = GetNetVal(nets, pins[0]);
        in1 = GetNetVal(nets, pins[1]);
        value = (in0 != in1) ? 1 : 0;
    }
    void WriteOutputs(std::map<std::pair<int, int>, int>& nets) const override
    {
        if (value) { auto pins = GetPins(); nets[{pins[2].x, pins[2].y}] = 1; }
    }
    void Draw(wxDC& dc) const override
    {
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = pos.x, y = pos.y;
        DrawBezierPath(dc, wxPoint(x - 20, y - 20), wxPoint(x - 5, y - 20),
            wxPoint(x - 5, y + 20), wxPoint(x - 20, y + 20));
        DrawBezierPath(dc, wxPoint(x - 20, y + 20), wxPoint(x - 5, y + 20),
            wxPoint(x + 10, y + 15), wxPoint(x + 20, y));
        DrawBezierPath(dc, wxPoint(x + 20, y), wxPoint(x + 10, y - 15),
            wxPoint(x - 5, y - 20), wxPoint(x - 20, y - 20));
        DrawBezierPath(dc, wxPoint(x - 30, y - 20), wxPoint(x - 15, y - 20),
            wxPoint(x - 15, y + 20), wxPoint(x - 30, y + 20));
    }
    bool HitTest(const wxPoint& p) const override
    {
        wxRect r(pos.x - 30, pos.y - 20, 50, 40);
        r.Inflate(2);
        return r.Contains(p);
    }
};


class XnorGateComponent : public Component
{
    int in0 = 0, in1 = 0;
public:
    explicit XnorGateComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("XNOR Gate"); }
    std::vector<wxPoint> GetPins() const override
    {
        return { {pos.x - 30, pos.y - 10}, {pos.x - 30, pos.y + 10}, {pos.x + 30, pos.y} };
    }
    void ReadInputs(const std::map<std::pair<int, int>, int>& nets) override
    {
        auto pins = GetPins();
        in0 = GetNetVal(nets, pins[0]);
        in1 = GetNetVal(nets, pins[1]);
        value = (in0 == in1) ? 1 : 0;
    }
    void WriteOutputs(std::map<std::pair<int, int>, int>& nets) const override
    {
        if (value) { auto pins = GetPins(); nets[{pins[2].x, pins[2].y}] = 1; }
    }
    void Draw(wxDC& dc) const override
    {
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = pos.x, y = pos.y;
        DrawBezierPath(dc, wxPoint(x - 20, y - 20), wxPoint(x - 5, y - 20),
            wxPoint(x - 5, y + 20), wxPoint(x - 20, y + 20));
        DrawBezierPath(dc, wxPoint(x - 20, y + 20), wxPoint(x - 5, y + 20),
            wxPoint(x + 10, y + 15), wxPoint(x + 20, y));
        DrawBezierPath(dc, wxPoint(x + 20, y), wxPoint(x + 10, y - 15),
            wxPoint(x - 5, y - 20), wxPoint(x - 20, y - 20));
        DrawBezierPath(dc, wxPoint(x - 30, y - 20), wxPoint(x - 15, y - 20),
            wxPoint(x - 15, y + 20), wxPoint(x - 30, y + 20));
        dc.DrawCircle(x + 25, y, 5);
    }
    bool HitTest(const wxPoint& p) const override
    {
        wxRect r(pos.x - 30, pos.y - 20, 60, 40);
        r.Inflate(2);
        return r.Contains(p);
    }
};


class OddParityComponent : public Component
{
    int inVal = 0;
public:
    explicit OddParityComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("Odd Parity"); }
    std::vector<wxPoint> GetPins() const override
    {
        return { {pos.x - 20, pos.y}, {pos.x + 20, pos.y} };
    }
    void ReadInputs(const std::map<std::pair<int, int>, int>& nets) override
    {
        auto pins = GetPins();
        inVal = GetNetVal(nets, pins[0]);
        value = inVal;
    }
    void WriteOutputs(std::map<std::pair<int, int>, int>& nets) const override
    {
        if (value) { auto pins = GetPins(); nets[{pins[1].x, pins[1].y}] = 1; }
    }
    void Draw(wxDC& dc) const override
    {
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = pos.x, y = pos.y;
        dc.DrawRectangle(x - 20, y - 10, 40, 20);
        dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.SetTextForeground(wxColour(0, 102, 204));
        dc.DrawText(wxT("2k+1"), x - 11, y - 6);
    }
    bool HitTest(const wxPoint& p) const override
    {
        wxRect r(pos.x - 20, pos.y - 10, 40, 20);
        r.Inflate(2);
        return r.Contains(p);
    }
};


class EvenParityComponent : public Component
{
    int in0 = 0, in1 = 0;
public:
    explicit EvenParityComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("Even Parity"); }
    std::vector<wxPoint> GetPins() const override
    {
        return { {pos.x - 20, pos.y - 10}, {pos.x - 20, pos.y + 10},
                 {pos.x + 20, pos.y - 10}, {pos.x + 20, pos.y + 10} };
    }
    void ReadInputs(const std::map<std::pair<int, int>, int>& nets) override
    {
        auto pins = GetPins();
        in0 = GetNetVal(nets, pins[0]);
        in1 = GetNetVal(nets, pins[1]);
        value = (in0 == in1) ? 1 : 0;
    }
    void WriteOutputs(std::map<std::pair<int, int>, int>& nets) const override
    {
        if (value)
        {
            auto pins = GetPins();
            nets[{pins[2].x, pins[2].y}] = 1;
            nets[{pins[3].x, pins[3].y}] = 1;
        }
    }
    void Draw(wxDC& dc) const override
    {
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = pos.x, y = pos.y;
        dc.DrawRectangle(x - 20, y - 10, 40, 20);
        dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.SetTextForeground(wxColour(0, 102, 204));
        dc.DrawText(wxT("2k"), x - 7, y - 6);
    }
    bool HitTest(const wxPoint& p) const override
    {
        wxRect r(pos.x - 20, pos.y - 10, 40, 20);
        r.Inflate(2);
        return r.Contains(p);
    }
};


class ControlledBufferComponent : public Component
{
    int inVal = 0, ctrlVal = 0;
public:
    explicit ControlledBufferComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("Controlled Buffer"); }
    std::vector<wxPoint> GetPins() const override
    {
        return { {pos.x - 20, pos.y}, {pos.x + 20, pos.y}, {pos.x, pos.y + 30} };
    }
    void ReadInputs(const std::map<std::pair<int, int>, int>& nets) override
    {
        auto pins = GetPins();
        inVal = GetNetVal(nets, pins[0]);
        ctrlVal = GetNetVal(nets, pins[2]);
        value = (ctrlVal && inVal) ? 1 : 0;
    }
    void WriteOutputs(std::map<std::pair<int, int>, int>& nets) const override
    {
        if (value) { auto pins = GetPins(); nets[{pins[1].x, pins[1].y}] = 1; }
    }
    void Draw(wxDC& dc) const override
    {
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = pos.x, y = pos.y;
        wxPoint tri[3] = {
            wxPoint(x - 20, y - 20),
            wxPoint(x - 20, y + 20),
            wxPoint(x + 20, y),
        };
        dc.DrawPolygon(3, tri);
        dc.DrawLine(x, y + 20, x, y + 30);
    }
    bool HitTest(const wxPoint& p) const override
    {
        wxRect r(pos.x - 20, pos.y - 20, 40, 50);
        r.Inflate(2);
        return r.Contains(p);
    }
};


class ControlledInverterComponent : public Component
{
    int inVal = 0, ctrlVal = 0;
public:
    explicit ControlledInverterComponent(const wxPoint& p) : Component(p) {}
    wxString GetType() const override { return wxT("Controlled Inverter"); }
    std::vector<wxPoint> GetPins() const override
    {
        return { {pos.x - 20, pos.y}, {pos.x + 30, pos.y}, {pos.x, pos.y + 30} };
    }
    void ReadInputs(const std::map<std::pair<int, int>, int>& nets) override
    {
        auto pins = GetPins();
        inVal = GetNetVal(nets, pins[0]);
        ctrlVal = GetNetVal(nets, pins[2]);
        value = (ctrlVal && !inVal) ? 1 : 0;
    }
    void WriteOutputs(std::map<std::pair<int, int>, int>& nets) const override
    {
        if (value) { auto pins = GetPins(); nets[{pins[1].x, pins[1].y}] = 1; }
    }
    void Draw(wxDC& dc) const override
    {
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = pos.x, y = pos.y;
        wxPoint tri[3] = {
            wxPoint(x - 20, y - 20),
            wxPoint(x - 20, y + 20),
            wxPoint(x + 20, y),
        };
        dc.DrawPolygon(3, tri);
        dc.DrawCircle(x + 25, y, 5);
        dc.DrawLine(x, y + 20, x, y + 30);
    }
    bool HitTest(const wxPoint& p) const override
    {
        wxRect r(pos.x - 20, pos.y - 20, 50, 50);
        r.Inflate(2);
        return r.Contains(p);
    }
};


struct Wire
{
    Component* startComp = nullptr;
    int        startPin = -1;
    wxPoint    startFree;
    wxPoint    end;
    bool       selected = false;
    int        value = 0;

    wxPoint StartPos() const
    {
        if (startComp)
        {
            std::vector<wxPoint> pins = startComp->GetPins();
            if (startPin >= 0 && startPin < (int)pins.size())
                return pins[startPin];
        }
        return startFree;
    }
    bool Intersects(const wxRect& box) const
    {
        const auto s = StartPos();
        wxRect horizontal(std::min(s.x, end.x), s.y, std::abs(end.x - s.x) + 1, 1);
        wxRect vertical(end.x, std::min(s.y, end.y), 1, std::abs(end.y - s.y) + 1);
        return box.Intersects(horizontal.Inflate(2)) || box.Intersects(vertical.Inflate(2));
    }
};


static Component* CreateComponent(const wxString& type, const wxPoint& pos)
{
    if (type == wxT("Pin")) return new PinComponent(pos);
    if (type == wxT("NOT Gate")) return new NotGateComponent(pos);
    if (type == wxT("Buffer")) return new BufferComponent(pos);
    if (type == wxT("AND Gate")) return new AndGateComponent(pos);
    if (type == wxT("OR Gate")) return new OrGateComponent(pos);
    if (type == wxT("NAND Gate")) return new NandGateComponent(pos);
    if (type == wxT("NOR Gate")) return new NorGateComponent(pos);
    if (type == wxT("XOR Gate")) return new XorGateComponent(pos);
    if (type == wxT("XNOR Gate")) return new XnorGateComponent(pos);
    if (type == wxT("Odd Parity")) return new OddParityComponent(pos);
    if (type == wxT("Even Parity")) return new EvenParityComponent(pos);
    if (type == wxT("Controlled Buffer")) return new ControlledBufferComponent(pos);
    if (type == wxT("Controlled Inverter")) return new ControlledInverterComponent(pos);
    return nullptr;
}


using NetPoint = std::pair<int, int>;
using NetValues = std::map<NetPoint, int>;

static NetPoint NetKey(const wxPoint& p) { return { p.x, p.y }; }

// Only coincident endpoints are connected, as in the editor. Propagate
// current drivers across each wire net; a wire must not keep its old high.
class WireNets
{
public:
    void Add(const wxPoint& a, const wxPoint& b)
    {
        const size_t i = Root(Index(NetKey(a)));
        const size_t j = Root(Index(NetKey(b)));
        if (i == j) return;
        if (m_size[i] < m_size[j])
        {
            m_parent[i] = j;
            m_size[j] += m_size[i];
        }
        else
        {
            m_parent[j] = i;
            m_size[i] += m_size[j];
        }
    }

    NetPoint NetOf(const wxPoint& p) const
    {
        const auto key = NetKey(p);
        const auto it = m_index.find(key);
        return it == m_index.end() ? key : m_points[Root(it->second)];
    }

    void Propagate(NetValues& nets) const
    {
        std::vector<bool> high(m_parent.size(), false);
        for (size_t i = 0; i < m_points.size(); ++i)
        {
            auto it = nets.find(m_points[i]);
            if (it != nets.end() && it->second) high[Root(i)] = true;
        }
        for (size_t i = 0; i < m_points.size(); ++i)
            if (high[Root(i)]) nets[m_points[i]] = 1;
    }

private:
    std::map<NetPoint, size_t> m_index;
    std::vector<NetPoint> m_points;
    std::vector<size_t> m_parent, m_size;

    size_t Index(const NetPoint& p)
    {
        auto added = m_index.emplace(p, m_points.size());
        if (added.second)
        {
            m_points.push_back(p);
            m_parent.push_back(added.first->second);
            m_size.push_back(1);
        }
        return added.first->second;
    }

    size_t Root(size_t i) const
    {
        while (m_parent[i] != i) i = m_parent[i];
        return i;
    }
};


// ---------------------------------------------------------------------------
// Netlist export (Protel 2.0 and KiCad)
// ---------------------------------------------------------------------------

struct NetlistPinRef
{
    size_t component = 0;
    size_t pin = 0;
};

struct NetlistNet
{
    wxString name;
    std::vector<NetlistPinRef> pins;
};

struct NetlistComponent
{
    wxString designator;
    wxString partToken;
    wxString description;
    std::vector<wxPoint> pins;
    bool isPin = false;
};

// Connection lines need a part-type token without spaces.
static wxString NetlistPartToken(const wxString& type)
{
    wxString token;
    for (size_t i = 0; i < type.length(); ++i)
    {
        const wxUniChar ch = type[i];
        const wxUint32 value = ch.GetValue();
        if ((value >= 'A' && value <= 'Z') ||
            (value >= 'a' && value <= 'z') ||
            (value >= '0' && value <= '9'))
            token += ch;
        else if (value == ' ' || value == '-' || value == '_' ||
                 value == '/' || value == '.')
            token += wxT('_');
    }

    while (token.Replace(wxT("__"), wxT("_")) > 0) {}
    if (token.empty())
        token = wxT("PART");
    if (token[0].GetValue() >= '0' && token[0].GetValue() <= '9')
        token.Prepend(wxT("PART_"));
    token.MakeUpper();
    return token;
}

// Wires connect coincident endpoints, matching the editor's simulation. Every
// pin belongs to exactly one net, including unconnected pins.
static bool BuildNetlistData(
    const std::vector<std::unique_ptr<Component>>& components,
    const std::vector<Wire>& wires,
    std::vector<NetlistComponent>& exported,
    std::vector<NetlistNet>& nets,
    wxString& error)
{
    error.clear();
    exported.clear();
    nets.clear();

    if (components.empty())
    {
        error = wxT("The circuit is empty. Add at least one component before exporting.");
        return false;
    }

    exported.reserve(components.size());

    int pinCount = 0;
    int gateCount = 0;
    for (const auto& component : components)
    {
        NetlistComponent item;
        item.pins = component->GetPins();
        item.isPin = dynamic_cast<const PinComponent*>(component.get()) != nullptr;
        item.description = component->GetType();
        if (item.isPin)
            item.partToken = wxT("PIN");
        else
            item.partToken = NetlistPartToken(item.description);
        item.designator = item.isPin
            ? wxString::Format(wxT("P%d"), ++pinCount)
            : wxString::Format(wxT("U%d"), ++gateCount);
        exported.push_back(std::move(item));
    }

    WireNets wiring;
    for (const auto& wire : wires)
        wiring.Add(wire.StartPos(), wire.end);

    std::map<NetPoint, std::vector<NetlistPinRef>> grouped;
    for (size_t ci = 0; ci < exported.size(); ++ci)
    {
        for (size_t pi = 0; pi < exported[ci].pins.size(); ++pi)
            grouped[wiring.NetOf(exported[ci].pins[pi])].push_back({ ci, pi });
    }

    nets.reserve(grouped.size());
    int anonymousNet = 1;
    for (const auto& entry : grouped)
    {
        NetlistNet net;
        net.pins = entry.second;
        for (const auto& ref : net.pins)
        {
            if (exported[ref.component].isPin)
            {
                net.name = exported[ref.component].designator;
                break;
            }
        }
        if (net.name.empty())
            net.name = wxString::Format(wxT("N%03d"), anonymousNet++);
        nets.push_back(std::move(net));
    }

    std::sort(nets.begin(), nets.end(), [](const NetlistNet& a, const NetlistNet& b)
        {
            return a.name.CmpNoCase(b.name) < 0;
        });

    return true;
}

static wxString BuildProtelNetlist(
    const std::vector<std::unique_ptr<Component>>& components,
    const std::vector<Wire>& wires,
    wxString& error)
{
    std::vector<NetlistComponent> exported;
    std::vector<NetlistNet> nets;
    if (!BuildNetlistData(components, wires, exported, nets, error))
        return {};

    wxString text;
    text << wxT("PROTEL NETLIST 2.0\r\n");

    for (const auto& item : exported)
    {
        const wxString footprint = wxString::Format(wxT("EDA_%s_%u"),
            item.partToken.c_str(), static_cast<unsigned>(item.pins.size()));

        text << wxT("[\r\n");
        text << wxT("DESIGNATOR\r\n") << item.designator << wxT("\r\n");
        text << wxT("FOOTPRINT\r\n") << footprint << wxT("\r\n");
        text << wxT("PARTTYPE\r\n") << item.partToken << wxT("\r\n");
        text << wxT("DESCRIPTION\r\n") << item.description << wxT("\r\n");
        for (int field = 1; field <= 16; ++field)
            text << wxString::Format(wxT("Part Field %d\r\n*\r\n"), field);
        for (int field = 1; field <= 8; ++field)
            text << wxString::Format(wxT("LIBRARYFIELD%d\r\n\r\n"), field);
        text << wxT("]\r\n");
    }

    for (const auto& net : nets)
    {
        text << wxT("(\r\n") << net.name << wxT("\r\n");
        for (const auto& ref : net.pins)
        {
            const auto& item = exported[ref.component];
            const unsigned number = static_cast<unsigned>(ref.pin + 1);
            text << wxString::Format(wxT("%s-%u %s-%u PASSIVE\r\n"),
                item.designator.c_str(), number,
                item.partToken.c_str(), number);
        }
        text << wxT(")\r\n");
    }

    return text;
}

// Map the editor's abstract components to KiCad's built-in footprints.
static wxString KiCadFootprint(const NetlistComponent& component)
{
    if (component.isPin)
        return wxT("TestPoint:TestPoint_Pad_D1.5mm");

    const size_t count = component.pins.size();
    if (count <= 8)  return wxT("Package_DIP:DIP-8_W7.62mm");
    if (count <= 14) return wxT("Package_DIP:DIP-14_W7.62mm");
    if (count <= 16) return wxT("Package_DIP:DIP-16_W7.62mm");
    if (count <= 18) return wxT("Package_DIP:DIP-18_W7.62mm");
    if (count <= 20) return wxT("Package_DIP:DIP-20_W7.62mm");
    if (count <= 22) return wxT("Package_DIP:DIP-22_W7.62mm");
    if (count <= 24) return wxT("Package_DIP:DIP-24_W15.24mm");
    if (count <= 26) return wxT("Package_DIP:DIP-26_W15.24mm");
    if (count <= 28) return wxT("Package_DIP:DIP-28_W15.24mm");
    if (count <= 32) return wxT("Package_DIP:DIP-32_W15.24mm");
    if (count <= 40) return wxT("Package_DIP:DIP-40_W15.24mm");
    if (count <= 42) return wxT("Package_DIP:DIP-42_W15.24mm");
    if (count <= 48) return wxT("Package_DIP:DIP-48_W15.24mm");
    if (count <= 64) return wxT("Package_DIP:DIP-64_W15.24mm");
    return {};
}

static wxString BuildKicadNetlist(
    const std::vector<std::unique_ptr<Component>>& components,
    const std::vector<Wire>& wires,
    wxString& error)
{
    std::vector<NetlistComponent> exported;
    std::vector<NetlistNet> nets;
    if (!BuildNetlistData(components, wires, exported, nets, error))
        return {};

    for (const auto& item : exported)
    {
        if (!item.isPin && item.pins.size() > 64)
        {
            error = wxString::Format(wxT("Component %s has %u pins; ")
                wxT("KiCad export supports at most 64 pins per component."),
                item.designator.c_str(), static_cast<unsigned>(item.pins.size()));
            return {};
        }
    }

    wxString text;
    text << wxT("(export (version \"E\")\n");
    text << wxT("  (components\n");

    for (const auto& item : exported)
    {
        text << wxT("    (comp (ref \"") << item.designator
            << wxT("\") (value \"") << item.partToken
            << wxT("\") (footprint \"") << KiCadFootprint(item)
            << wxT("\"))\n");
    }

    text << wxT("  )\n");
    text << wxT("  (nets\n");

    int code = 1;
    for (const auto& net : nets)
    {
        text << wxT("    (net (code \"") << code++ << wxT("\") (name \"")
            << net.name << wxT("\")\n");
        for (const auto& ref : net.pins)
        {
            const auto& item = exported[ref.component];
            const unsigned number = static_cast<unsigned>(ref.pin + 1);
            text << wxT("      (node (ref \"") << item.designator
                << wxT("\") (pin \"") << number << wxT("\"))\n");
        }
        text << wxT("    )\n");
    }

    text << wxT("  )\n");
    text << wxT(")\n");
    return text;
}

static bool WriteNetlistFile(const wxString& path, const wxString& text, wxString& error)
{
    wxFile file;
    if (!file.Create(path, true))
    {
        error = wxString::Format(wxT("Could not create the file:\n%s"), path.c_str());
        return false;
    }

    const wxScopedCharBuffer utf8 = text.utf8_str();
    if (file.Write(utf8.data(), utf8.length()) != utf8.length())
    {
        error = wxString::Format(wxT("Could not write the file:\n%s"), path.c_str());
        return false;
    }

    if (!file.Flush())
    {
        error = wxString::Format(wxT("Could not finish writing the file:\n%s"), path.c_str());
        return false;
    }

    return true;
}

static bool SettleCircuit(const std::vector<std::unique_ptr<Component>>& components,
    const WireNets& wires, const NetValues& inputs, size_t passes, NetValues& nets)
{
    nets = inputs;
    wires.Propagate(nets);
    for (size_t pass = 0; pass < passes; ++pass)
    {
        NetValues next = inputs;
        for (const auto& component : components)
        {
            component->ReadInputs(nets);
            component->WriteOutputs(next);
        }
        wires.Propagate(next);
        if (next == nets) return true;
        nets.swap(next);
    }
    return false;
}

// Session-only snapshot: built-in gates and wire nets, with selected Pin
// markers mapped to the package ports. Marker toggle values are not copied.
struct CustomDefinition
{
    wxString name;
    std::vector<std::pair<wxString, wxPoint>> gates;
    std::vector<wxPoint> inputs, outputs;
    WireNets wiring;
};

class CustomComponent : public Component
{
public:
    CustomComponent(std::shared_ptr<const CustomDefinition> definition, const wxPoint& p)
        : Component(p), m_definition(std::move(definition)),
        m_outputs(m_definition->outputs.size(), 0)
    {
        for (const auto& gate : m_definition->gates)
            m_gates.emplace_back(CreateComponent(gate.first, gate.second));
    }

    wxString GetType() const override { return m_definition->name; }

    std::vector<wxPoint> GetPins() const override
    {
        std::vector<wxPoint> pins;
        for (size_t i = 0; i < m_definition->inputs.size(); ++i)
            pins.push_back(Port(true, i));
        for (size_t i = 0; i < m_definition->outputs.size(); ++i)
            pins.push_back(Port(false, i));
        return pins;
    }

    void ReadInputs(const NetValues& nets) override
    {
        NetValues inputs, internal;
        for (size_t i = 0; i < m_definition->inputs.size(); ++i)
            if (GetNetVal(nets, Port(true, i)))
                inputs[NetKey(m_definition->inputs[i])] = 1;

        // Combinational logic settles within one pass per gate plus a check.
        // Feedback and stateful circuits are unsupported; unsettled outputs are low.
        const bool settled = SettleCircuit(
            m_gates, m_definition->wiring, inputs, m_gates.size() + 1, internal);
        for (size_t i = 0; i < m_outputs.size(); ++i)
            m_outputs[i] = settled ? GetNetVal(internal, m_definition->outputs[i]) : 0;
    }

    void WriteOutputs(NetValues& nets) const override
    {
        for (size_t i = 0; i < m_outputs.size(); ++i)
            if (m_outputs[i]) nets[NetKey(Port(false, i))] = 1;
    }

    wxRect GetBounds() const override
    {
        const int halfRows = static_cast<int>(
            std::max(m_definition->inputs.size(), m_definition->outputs.size())) * 10;
        return wxRect(pos.x - HalfWidth(), pos.y - halfRows - 30,
            2 * HalfWidth() + 1, 2 * halfRows + 41);
    }

    bool HitTest(const wxPoint& p) const override { return GetBounds().Contains(p); }

    void Draw(wxDC& dc) const override
    {
        const auto bounds = GetBounds();
        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(wxBrush(wxColour(236, 245, 255)));
        dc.DrawRectangle(bounds);
        dc.SetTextForeground(*wxBLACK);
        const wxSize title = dc.GetTextExtent(m_definition->name);
        dc.DrawText(m_definition->name, pos.x - title.x / 2, bounds.GetTop() + 6);
        for (size_t i = 0; i < m_definition->inputs.size(); ++i)
        {
            const auto p = Port(true, i);
            dc.DrawText(wxString::Format(wxT("I%u"), static_cast<unsigned>(i + 1)),
                p.x + 7, p.y - dc.GetCharHeight() / 2);
        }
        for (size_t i = 0; i < m_definition->outputs.size(); ++i)
        {
            const auto p = Port(false, i);
            const auto label = wxString::Format(wxT("O%u"), static_cast<unsigned>(i + 1));
            dc.DrawText(label, p.x - 7 - dc.GetTextExtent(label).x,
                p.y - dc.GetCharHeight() / 2);
        }
    }

private:
    std::shared_ptr<const CustomDefinition> m_definition;
    std::vector<std::unique_ptr<Component>> m_gates;
    std::vector<int> m_outputs;

    int HalfWidth() const
    {
        return std::max(40, static_cast<int>(m_definition->name.length()) * 10);
    }

    wxPoint Port(bool input, size_t index) const
    {
        const auto count = input ? m_definition->inputs.size() : m_definition->outputs.size();
        return wxPoint(pos.x + (input ? -HalfWidth() : HalfWidth()),
            pos.y + static_cast<int>(index) * 20 - (static_cast<int>(count) - 1) * 10);
    }
};

class CustomTreeItem : public wxTreeItemData
{
public:
    explicit CustomTreeItem(std::shared_ptr<const CustomDefinition> d)
        : definition(std::move(d)) {
    }
    std::shared_ptr<const CustomDefinition> definition;
};


class CircuitCanvas : public wxScrolledWindow
{
public:
    enum Mode { MODE_NORMAL, MODE_SELECT, MODE_LINE };

    CircuitCanvas(wxWindow* parent)
        : wxScrolledWindow(parent,
            wxID_ANY,
            wxDefaultPosition,
            wxDefaultSize,
            wxHSCROLL | wxVSCROLL)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetVirtualSize(3000, 3000);
        SetScrollRate(10, 10);

        Bind(wxEVT_PAINT, &CircuitCanvas::OnPaint, this);
        Bind(wxEVT_MOTION, &CircuitCanvas::OnMouseMove, this);
        Bind(wxEVT_LEFT_DOWN, &CircuitCanvas::OnLeftDown, this);
        Bind(wxEVT_LEFT_UP, &CircuitCanvas::OnLeftUp, this);
        Bind(wxEVT_KEY_DOWN, &CircuitCanvas::OnKeyDown, this);
    }

    void SetMode(Mode m) { m_mode = m; }

    void AddComponent(const wxString& type)
    {
        wxSize sz = GetClientSize();
        wxPoint center = CalcUnscrolledPosition(wxPoint(sz.x / 2, sz.y / 2));

        Component* c = CreateComponent(type, center);
        if (!c) return;

        AddToCanvas(std::unique_ptr<Component>(c));
    }

    void AddCustomComponent(const std::shared_ptr<const CustomDefinition>& definition)
    {
        const auto size = GetClientSize();
        const auto center = CalcUnscrolledPosition(wxPoint(size.x / 2, size.y / 2));
        AddToCanvas(std::make_unique<CustomComponent>(definition,
            wxPoint(SnapGrid(center.x), SnapGrid(center.y))));
    }

    std::shared_ptr<CustomDefinition> PackageSelection()
    {
        std::vector<wxPoint> markers;
        std::vector<Component*> gates;
        for (const auto& component : m_components)
        {
            if (!component->selected) continue;
            if (dynamic_cast<CustomComponent*>(component.get()))
                return PackageError(wxT("Nested custom components are not supported."));
            if (dynamic_cast<PinComponent*>(component.get()))
                markers.push_back(component->pos);
            else
                gates.push_back(component.get());
        }
        if (gates.empty() || markers.size() < 2)
            return PackageError(wxT("Box-select at least one gate, two Pin components, ")
                wxT("and the wires that connect them. Use Pin components as the package ports."));

        // Assign I1, I2, ... and O1, O2, ... in top-to-bottom order.
        std::sort(markers.begin(), markers.end(), [](const wxPoint& a, const wxPoint& b)
            {
                return a.y == b.y ? a.x < b.x : a.y < b.y;
            });
        if (std::adjacent_find(markers.begin(), markers.end()) != markers.end())
            return PackageError(wxT("Place the port markers at distinct positions."));

        auto definition = std::make_shared<CustomDefinition>();
        const auto origin = markers.front();
        bool hasWires = false;
        for (const auto& wire : m_wires)
        {
            if (!wire.selected) continue;
            definition->wiring.Add(wire.StartPos() - origin, wire.end - origin);
            hasWires = true;
        }
        if (!hasWires)
            return PackageError(wxT("Include the connecting wires in the box selection."));

        wxArrayString choices;
        for (size_t i = 0; i < markers.size(); ++i)
            choices.Add(wxString::Format(wxT("Pin %u (%d, %d)"),
                static_cast<unsigned>(i + 1), markers[i].x, markers[i].y));

        wxMultiChoiceDialog dialog(this,
            wxT("Check the input pins (I). Unchecked pins become outputs (O)."),
            wxT("Define package ports"), choices);
        m_packageMarkers = markers;
        Refresh();
        Update();
        const int result = dialog.ShowModal();
        m_packageMarkers.clear();
        Refresh();
        if (result != wxID_OK) return {};

        std::vector<bool> isInput(markers.size(), false);
        for (int index : dialog.GetSelections()) isInput[index] = true;
        for (size_t i = 0; i < markers.size(); ++i)
        {
            auto& ports = isInput[i] ? definition->inputs : definition->outputs;
            ports.push_back(markers[i] - origin);
        }
        if (definition->inputs.empty() || definition->outputs.empty())
            return PackageError(wxT("Choose at least one input and leave at least one output."));

        // Check the selected port directions against the captured wiring only.
        std::set<NetPoint> gateInputs, gateOutputs, inputPorts, outputPorts;
        for (const auto* gate : gates)
        {
            const auto type = gate->GetType();
            definition->gates.emplace_back(type, gate->pos - origin);
            const auto pins = gate->GetPins();
            for (size_t i = 0; i < pins.size(); ++i)
            {
                bool output = i + 1 == pins.size();
                if (type == wxT("Even Parity")) output = i >= 2;
                if (type == wxT("Controlled Buffer") || type == wxT("Controlled Inverter"))
                    output = i == 1;
                (output ? gateOutputs : gateInputs).insert(
                    definition->wiring.NetOf(pins[i] - origin));
            }
        }
        for (const auto& pin : definition->outputs)
            outputPorts.insert(definition->wiring.NetOf(pin));
        for (const auto& pin : definition->inputs)
        {
            const auto net = definition->wiring.NetOf(pin);
            if (gateOutputs.count(net) || !inputPorts.insert(net).second ||
                (!gateInputs.count(net) && !outputPorts.count(net)))
                return PackageError(wxT("Each input Pin must drive a gate input or an output Pin ")
                    wxT("on its own net, without sharing a net with a gate output."));
        }
        for (const auto& net : outputPorts)
            if (!gateOutputs.count(net) && !inputPorts.count(net))
                return PackageError(wxT("Each output Pin must be driven by a selected gate output ")
                    wxT("or an input Pin. Check the wire endpoints and the selected port directions."));

        return definition;
    }

    bool ExportProtelNetlist(const wxString& path, wxString& error) const
    {
        const wxString text = BuildProtelNetlist(m_components, m_wires, error);
        if (text.empty())
            return false;
        return WriteNetlistFile(path, text, error);
    }

    bool ExportKicadNetlist(const wxString& path, wxString& error) const
    {
        const wxString text = BuildKicadNetlist(m_components, m_wires, error);
        if (text.empty())
            return false;
        return WriteNetlistFile(path, text, error);
    }


private:
    Mode m_mode = MODE_NORMAL;

    std::vector<std::unique_ptr<Component>> m_components;
    std::vector<Wire>                       m_wires;

    int     m_draggingIndex = -1;
    wxPoint m_dragOffset;
    wxPoint m_clickStartPos;

    bool       m_drawingWire = false;
    Component* m_wireSrcComp = nullptr;
    int        m_wireSrcPin = -1;
    wxPoint    m_wireStartFree;
    wxPoint    m_wireEnd;

    bool    m_boxSelecting = false;
    wxPoint m_boxStart;
    wxPoint m_boxEnd;

    std::vector<wxPoint> m_packageMarkers;

    std::shared_ptr<CustomDefinition> PackageError(const wxString& message)
    {
        wxMessageBox(message, wxT("Create Custom Component"), wxOK | wxICON_WARNING, this);
        return {};
    }

    void AddToCanvas(std::unique_ptr<Component> component)
    {
        m_components.push_back(std::move(component));
        Simulate();
        Refresh();
    }

    void Simulate()
    {
        WireNets wiring;
        for (const auto& wire : m_wires) wiring.Add(wire.StartPos(), wire.end);

        NetValues nets;
        SettleCircuit(m_components, wiring, {}, 30, nets);
        for (const auto& component : m_components) component->ReadInputs(nets);
        for (auto& wire : m_wires) wire.value = GetNetVal(nets, wire.StartPos());
    }

    bool FindWireStart(const wxPoint& p,
        Component*& comp, int& pin, wxPoint& freePos)
    {
        int  bestDist2 = 6 * 6;
        bool found = false;

        for (size_t i = 0; i < m_wires.size(); ++i)
        {
            int dx = p.x - m_wires[i].end.x;
            int dy = p.y - m_wires[i].end.y;
            int d2 = dx * dx + dy * dy;
            if (d2 <= bestDist2)
            {
                bestDist2 = d2;
                comp = nullptr;
                pin = -1;
                freePos = m_wires[i].end;
                found = true;
            }
        }
        for (size_t i = 0; i < m_components.size(); ++i)
        {
            std::vector<wxPoint> pins = m_components[i]->GetPins();
            for (size_t j = 0; j < pins.size(); ++j)
            {
                int dx = p.x - pins[j].x;
                int dy = p.y - pins[j].y;
                int d2 = dx * dx + dy * dy;
                if (d2 <= bestDist2)
                {
                    bestDist2 = d2;
                    comp = m_components[i].get();
                    pin = (int)j;
                    found = true;
                }
            }
        }
        return found;
    }

    static void DrawLWire(wxDC& dc, const wxPoint& s, const wxPoint& e)
    {
        dc.DrawLine(s.x, s.y, e.x, s.y);
        dc.DrawLine(e.x, s.y, e.x, e.y);
    }

    static int SnapGrid(int v)
    {
        const int grid = 10;
        return (v >= 0) ? ((v + grid / 2) / grid) * grid
            : -(((-v) + grid / 2) / grid) * grid;
    }

    void PreviewWireSnap(wxDC& dc)
    {
        if (!m_drawingWire) return;

        wxPoint snapPos;
        snapPos.x = SnapGrid(m_wireEnd.x);
        snapPos.y = SnapGrid(m_wireEnd.y);

        dc.SetPen(wxPen(*wxRED, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawCircle(snapPos, 4);
    }

    void OnMouseMove(wxMouseEvent& event)
    {
        wxPoint p = CalcUnscrolledPosition(event.GetPosition());
        if (m_boxSelecting)
        {
            m_boxEnd = p;
            Refresh();
        }
        if (m_drawingWire)
        {
            m_wireEnd = p;
            Refresh();
        }
        else if (m_draggingIndex >= 0)
        {
            m_components[m_draggingIndex]->pos = p - m_dragOffset;
            Refresh();
        }
        event.Skip();
    }

    void OnLeftDown(wxMouseEvent& event)
    {
        SetFocus();
        wxPoint p = CalcUnscrolledPosition(event.GetPosition());
        m_clickStartPos = p;

        // ============ Line 模式：只处理接线点，开始画电线 ============
        if (m_mode == MODE_LINE)
        {
            Component* srcComp = nullptr;
            int        srcPin = -1;
            wxPoint    srcFree;
            if (FindWireStart(p, srcComp, srcPin, srcFree))
            {
                m_drawingWire = true;
                m_wireSrcComp = srcComp;
                m_wireSrcPin = srcPin;
                m_wireStartFree = srcFree;
                m_wireEnd = p;
                Refresh();
            }
            event.Skip();
            return;
        }

        // ============ Select 模式：不拖动，等待 OnLeftUp 判断单击 Pin ============
        if (m_mode == MODE_SELECT)
        {
            bool hitComp = false;
            for (int i = (int)m_components.size() - 1; i >= 0; --i)
            {
                if (m_components[i]->HitTest(p)) { hitComp = true; break; }
            }
            if (!hitComp)
            {
                m_boxSelecting = true;
                m_boxStart = p;
                m_boxEnd = p;
                for (size_t i = 0; i < m_components.size(); ++i)
                    m_components[i]->selected = false;
                for (size_t i = 0; i < m_wires.size(); ++i)
                    m_wires[i].selected = false;
            }
            event.Skip();
            return;
        }

        // ============ Normal 模式：拖动元件 + 空白框选 ============
        bool hitComponent = false;
        for (int i = (int)m_components.size() - 1; i >= 0; --i)
        {
            if (m_components[i]->HitTest(p))
            {
                m_draggingIndex = i;
                m_dragOffset = p - m_components[i]->pos;
                hitComponent = true;
                break;
            }
        }

        if (!hitComponent)
        {
            m_boxSelecting = true;
            m_boxStart = p;
            m_boxEnd = p;
            for (size_t i = 0; i < m_components.size(); ++i)
                m_components[i]->selected = false;
            for (size_t i = 0; i < m_wires.size(); ++i)
                m_wires[i].selected = false;
        }
        event.Skip();
    }

    void OnLeftUp(wxMouseEvent& event)
    {
        // ---- 框选结束 ----
        if (m_boxSelecting)
        {
            m_boxSelecting = false;
            m_boxEnd = CalcUnscrolledPosition(event.GetPosition());

            wxRect box(
                std::min(m_boxStart.x, m_boxEnd.x),
                std::min(m_boxStart.y, m_boxEnd.y),
                std::abs(m_boxStart.x - m_boxEnd.x) + 1,
                std::abs(m_boxStart.y - m_boxEnd.y) + 1);

            if (box.width >= 4 || box.height >= 4)
            {
                for (size_t i = 0; i < m_components.size(); ++i)
                {
                    wxRect cb = m_components[i]->GetBounds();
                    cb.Inflate(2);
                    if (box.Intersects(cb)) m_components[i]->selected = true;
                }
                for (size_t i = 0; i < m_wires.size(); ++i)
                {
                    if (m_wires[i].Intersects(box)) m_wires[i].selected = true;
                }
            }
            Refresh();
            m_draggingIndex = -1;
            event.Skip();
            return;
        }

        // ---- 画电线固定 ----
        if (m_drawingWire)
        {
            const auto release = CalcUnscrolledPosition(event.GetPosition());
            Component* target = nullptr;
            int targetPin = -1;
            wxPoint junction;
            if (FindWireStart(release, target, targetPin, junction))
                m_wireEnd = target ? target->GetPins()[targetPin] : junction;
            else
                m_wireEnd = wxPoint(SnapGrid(release.x), SnapGrid(release.y));

            Wire w;
            w.startComp = m_wireSrcComp;
            w.startPin = m_wireSrcPin;
            w.startFree = m_wireStartFree;
            w.end = m_wireEnd;
            m_wires.push_back(w);

            m_drawingWire = false;
            m_wireSrcComp = nullptr;
            m_wireSrcPin = -1;
            m_draggingIndex = -1;
            Simulate();
            Refresh();
            event.Skip();
            return;
        }

        // ---- Select 模式：单击切换 Pin ----
        if (m_mode == MODE_SELECT)
        {
            wxPoint p = CalcUnscrolledPosition(event.GetPosition());
            int dx = p.x - m_clickStartPos.x;
            int dy = p.y - m_clickStartPos.y;
            if (dx * dx + dy * dy <= 9)
            {
                for (int i = (int)m_components.size() - 1; i >= 0; --i)
                {
                    if (m_components[i]->HitTest(p))
                    {
                        PinComponent* pin = dynamic_cast<PinComponent*>(m_components[i].get());
                        if (pin) pin->ToggleValue();
                        Simulate();
                        Refresh();
                        break;
                    }
                }
            }
            m_draggingIndex = -1;
            event.Skip();
            return;
        }

        // ---- Normal 模式：拖动结束吸附网格 ----
        if (m_draggingIndex >= 0)
        {
            Component* c = m_components[m_draggingIndex].get();
            c->pos.x = SnapGrid(c->pos.x);
            c->pos.y = SnapGrid(c->pos.y);
            Simulate();
            Refresh();
        }
        m_draggingIndex = -1;
        event.Skip();
    }

    void OnKeyDown(wxKeyEvent& event)
    {
        int code = event.GetKeyCode();
        if (code != WXK_BACK && code != WXK_DELETE)
        {
            event.Skip();
            return;
        }

        std::vector<Wire> keptWires;
        for (size_t i = 0; i < m_wires.size(); ++i)
            if (!m_wires[i].selected) keptWires.push_back(m_wires[i]);
        m_wires = keptWires;

        std::vector<Component*> removed;
        std::vector<std::unique_ptr<Component>> keptComps;
        for (size_t i = 0; i < m_components.size(); ++i)
        {
            if (m_components[i]->selected)
                removed.push_back(m_components[i].get());
            else
                keptComps.push_back(std::move(m_components[i]));
        }
        m_components = std::move(keptComps);

        std::vector<Wire> keptWires2;
        for (size_t i = 0; i < m_wires.size(); ++i)
        {
            bool orphan = false;
            for (size_t j = 0; j < removed.size(); ++j)
                if (m_wires[i].startComp == removed[j]) { orphan = true; break; }
            if (!orphan) keptWires2.push_back(m_wires[i]);
        }
        m_wires = keptWires2;

        Simulate();
        Refresh();
    }

    void OnPaint(wxPaintEvent& event)
    {
        wxAutoBufferedPaintDC dc(this);
        PrepareDC(dc);

        wxSize clientSize = GetClientSize();
        wxPoint viewStart = GetViewStart();

        dc.SetBackground(wxBrush(*wxWHITE));
        dc.Clear();

        dc.SetPen(wxPen(wxColour(200, 200, 200), 1));

        const int gridSize = 10;
        int startX = (viewStart.x * 10) / gridSize * gridSize;
        int startY = (viewStart.y * 10) / gridSize * gridSize;
        int endX = startX + clientSize.x + gridSize;
        int endY = startY + clientSize.y + gridSize;

        for (int x = startX; x < endX; x += gridSize)
            for (int y = startY; y < endY; y += gridSize)
                dc.DrawPoint(x, y);

        dc.SetPen(wxPen(*wxBLACK, 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        for (size_t i = 0; i < m_components.size(); ++i)
            m_components[i]->Draw(dc);

        for (size_t i = 0; i < m_wires.size(); ++i)
        {
            wxColour col = m_wires[i].value ? wxColour(0, 220, 0) : *wxBLACK;
            dc.SetPen(wxPen(col, 2));
            DrawLWire(dc, m_wires[i].StartPos(), m_wires[i].end);
        }

        if (m_drawingWire)
        {
            wxPoint s;
            if (m_wireSrcComp)
            {
                std::vector<wxPoint> pins = m_wireSrcComp->GetPins();
                if (m_wireSrcPin >= 0 && m_wireSrcPin < (int)pins.size())
                    s = pins[m_wireSrcPin];
            }
            else
            {
                s = m_wireStartFree;
            }
            dc.SetPen(wxPen(*wxBLACK, 2, wxPENSTYLE_DOT));
            DrawLWire(dc, s, m_wireEnd);
            PreviewWireSnap(dc);
        }

        // 元件接线点（小圆点）
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(*wxBLACK));
        for (size_t i = 0; i < m_components.size(); ++i)
        {
            std::vector<wxPoint> pins = m_components[i]->GetPins();
            for (size_t j = 0; j < pins.size(); ++j)
                dc.DrawCircle(pins[j], 3);
        }
        // 电线末端接线点（半径改为 2，更小更平滑）
        for (size_t i = 0; i < m_wires.size(); ++i)
            dc.DrawCircle(m_wires[i].end, 2);

        dc.SetPen(wxPen(wxColour(0, 120, 255), 1, wxPENSTYLE_DOT));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        for (size_t i = 0; i < m_components.size(); ++i)
        {
            if (m_components[i]->selected)
            {
                wxRect r = m_components[i]->GetBounds();
                r.Inflate(3);
                dc.DrawRectangle(r);
            }
        }

        for (size_t i = 0; i < m_wires.size(); ++i)
        {
            if (m_wires[i].selected)
            {
                dc.SetPen(wxPen(wxColour(255, 0, 0), 3));
                DrawLWire(dc, m_wires[i].StartPos(), m_wires[i].end);
            }
        }

        for (size_t i = 0; i < m_packageMarkers.size(); ++i)
        {
            const auto& p = m_packageMarkers[i];
            dc.SetTextForeground(wxColour(0, 80, 180));
            dc.DrawText(wxString::Format(wxT("Pin %u"), static_cast<unsigned>(i + 1)),
                p.x + 12, p.y - dc.GetCharHeight());
        }

        if (m_boxSelecting)
        {
            int x1 = std::min(m_boxStart.x, m_boxEnd.x);
            int y1 = std::min(m_boxStart.y, m_boxEnd.y);
            int x2 = std::max(m_boxStart.x, m_boxEnd.x);
            int y2 = std::max(m_boxStart.y, m_boxEnd.y);
            dc.SetPen(wxPen(wxColour(0, 0, 200), 1, wxPENSTYLE_SHORT_DASH));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRectangle(x1, y1, x2 - x1, y2 - y1);
        }
    }
};


class CircuitEditorApp : public wxApp
{
public:
    virtual bool OnInit() override;
};

IMPLEMENT_APP(CircuitEditorApp)
DECLARE_APP(CircuitEditorApp)


class MainFrame : public wxFrame
{
public:
    MainFrame(const wxString& title);

private:
    wxMenuBar* m_menuBar;
    wxToolBar* m_toolBar;
    wxSplitterWindow* m_splitterOuter;
    wxSplitterWindow* m_splitterLeft;
    wxTreeCtrl* m_treeProject;
    wxTreeItemId m_customFolder;
    wxPanel* m_panelProp;
    CircuitCanvas* m_canvas;

    void BuildMenuBar();
    void BuildToolBar();
    void BuildSplitterLayout();
    void BuildStatusBar();
    void BuildComponentTree(wxWindow* parent);
    void BuildPropertyPanel(wxWindow* parent);
    void OnTreeLeftDown(wxMouseEvent& event);
    void OnCreateCustom(wxCommandEvent& event);
    void OnExportNetlist(wxCommandEvent& event);
    void OnExportKicadNetlist(wxCommandEvent& event);
    void ExportNetlist(bool kicad);

    enum {
        ID_TOOL_PIN = wxID_HIGHEST + 1,
        ID_TOOL_SELECT,
        ID_TOOL_LINE,
        ID_CREATE_CUSTOM,
        ID_EXPORT_NETLIST,
        ID_EXPORT_KICAD_NETLIST
    };
    void OnToolPin(wxCommandEvent& event);
    void OnToolSelect(wxCommandEvent& event);
    void OnToolLine(wxCommandEvent& event);
};


MainFrame::MainFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(1200, 800))
{
    BuildMenuBar();
    BuildToolBar();
    BuildSplitterLayout();
    BuildStatusBar();
}

void MainFrame::BuildMenuBar()
{
    m_menuBar = new wxMenuBar;

    wxMenu* menuFile = new wxMenu();
    wxMenu* menuEdit = new wxMenu();
    wxMenu* menuProject = new wxMenu();
    wxMenu* menuSimulate = new wxMenu();
    wxMenu* menuWindow = new wxMenu();
    wxMenu* menuHelp = new wxMenu();

    menuFile->Append(wxID_NEW, wxT("&New"));
    menuFile->Append(wxID_OPEN, wxT("&Open"));
    menuFile->Append(wxID_SAVE, wxT("&Save"));
    menuFile->AppendSeparator();
    menuFile->Append(ID_EXPORT_NETLIST, wxT("Export &Netlist (Protel)...\tCtrl+E"));
    menuFile->Append(ID_EXPORT_KICAD_NETLIST, wxT("Export Netlist (&KiCad)...\tCtrl+Shift+E"));
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT, wxT("E&xit"));

    Bind(wxEVT_MENU, &MainFrame::OnExportNetlist, this, ID_EXPORT_NETLIST);
    Bind(wxEVT_MENU, &MainFrame::OnExportKicadNetlist, this, ID_EXPORT_KICAD_NETLIST);

    menuProject->Append(ID_CREATE_CUSTOM, wxT("Create &Custom Component..."));
    Bind(wxEVT_MENU, &MainFrame::OnCreateCustom, this, ID_CREATE_CUSTOM);

    m_menuBar->Append(menuFile, wxT("&File"));
    m_menuBar->Append(menuEdit, wxT("&Edit"));
    m_menuBar->Append(menuProject, wxT("&Project"));
    m_menuBar->Append(menuSimulate, wxT("&Simulate"));
    m_menuBar->Append(menuWindow, wxT("&Window"));
    m_menuBar->Append(menuHelp, wxT("&Help"));

    SetMenuBar(m_menuBar);
}

void MainFrame::BuildToolBar()
{
    m_toolBar = CreateToolBar(wxTB_HORIZONTAL | wxTB_FLAT);

    // ---- Select 按钮（可切换模式） ----
    m_toolBar->AddCheckTool(ID_TOOL_SELECT, wxT("Select"),
        wxArtProvider::GetBitmap(wxART_ADD_BOOKMARK),
        wxNullBitmap, wxT("Select mode: click Pin to toggle its value"));
    m_toolBar->Bind(wxEVT_TOOL, &MainFrame::OnToolSelect, this, ID_TOOL_SELECT);

    // ---- Line 按钮（可切换模式） ----
    m_toolBar->AddCheckTool(ID_TOOL_LINE, wxT("Line"),
        wxArtProvider::GetBitmap(wxART_LIST_VIEW),
        wxNullBitmap, wxT("Line mode: draw / extend wires"));
    m_toolBar->Bind(wxEVT_TOOL, &MainFrame::OnToolLine, this, ID_TOOL_LINE);

    m_toolBar->AddSeparator();

    // ---- Pin 按钮（点击添加 Pin 元件） ----
    wxBitmap pinBmp(16, 16);
    {
        wxMemoryDC mem(pinBmp);
        mem.SetBackground(*wxWHITE_BRUSH);
        mem.Clear();
        mem.SetPen(wxPen(*wxBLACK));
        mem.SetBrush(*wxTRANSPARENT_BRUSH);
        mem.DrawRectangle(1, 1, 14, 14);
        mem.SetPen(*wxTRANSPARENT_PEN);
        mem.SetBrush(*wxBLACK_BRUSH);
        mem.DrawCircle(8, 8, 2);
    }

    m_toolBar->AddTool(ID_TOOL_PIN, wxT("Pin"), pinBmp, wxT("Add Pin"));
    m_toolBar->Bind(wxEVT_TOOL, &MainFrame::OnToolPin, this, ID_TOOL_PIN);

    m_toolBar->AddTool(wxID_ANY, wxT("Run"),
        wxArtProvider::GetBitmap(wxART_GO_FORWARD));

    m_toolBar->Realize();
}

void MainFrame::BuildSplitterLayout()
{
    m_splitterOuter = new wxSplitterWindow(this, wxID_ANY,
        wxDefaultPosition, wxDefaultSize, wxSP_3D);
    m_splitterOuter->SetMinimumPaneSize(150);

    m_splitterLeft = new wxSplitterWindow(m_splitterOuter, wxID_ANY,
        wxDefaultPosition, wxDefaultSize, wxSP_3D);
    m_splitterLeft->SetMinimumPaneSize(100);

    m_treeProject = new wxTreeCtrl(m_splitterLeft, wxID_ANY,
        wxDefaultPosition, wxDefaultSize,
        wxTR_HAS_BUTTONS | wxTR_SINGLE);
    BuildComponentTree(m_treeProject);
    m_treeProject->Bind(wxEVT_LEFT_DOWN, &MainFrame::OnTreeLeftDown, this);

    m_panelProp = new wxPanel(m_splitterLeft, wxID_ANY);
    m_panelProp->SetBackgroundColour(wxColour(245, 245, 245));
    BuildPropertyPanel(m_panelProp);

    m_splitterLeft->SplitHorizontally(m_treeProject, m_panelProp, 400);

    m_canvas = new CircuitCanvas(m_splitterOuter);

    m_splitterOuter->SplitVertically(m_splitterLeft, m_canvas, 280);
}

void MainFrame::BuildStatusBar()
{
    CreateStatusBar(2);
    SetStatusText(wxT("100%"), 0);
}

void MainFrame::BuildComponentTree(wxWindow* parent)
{
    wxTreeCtrl* tree = static_cast<wxTreeCtrl*>(parent);

    wxTreeItemId root = tree->AddRoot(wxT("Untitled"));
    wxTreeItemId nodeWiring = tree->AppendItem(root, wxT("Wiring"));
    wxTreeItemId nodeGates = tree->AppendItem(root, wxT("Gates"));
    m_customFolder = tree->AppendItem(root, wxT("Custom Components"));

    tree->AppendItem(nodeGates, wxT("AND Gate"));
    tree->AppendItem(nodeGates, wxT("OR Gate"));
    tree->AppendItem(nodeGates, wxT("NOT Gate"));
    tree->AppendItem(nodeGates, wxT("Buffer"));
    tree->AppendItem(nodeGates, wxT("NAND Gate"));
    tree->AppendItem(nodeGates, wxT("NOR Gate"));
    tree->AppendItem(nodeGates, wxT("XOR Gate"));
    tree->AppendItem(nodeGates, wxT("XNOR Gate"));
    tree->AppendItem(nodeGates, wxT("Odd Parity"));
    tree->AppendItem(nodeGates, wxT("Even Parity"));
    tree->AppendItem(nodeGates, wxT("Controlled Buffer"));
    tree->AppendItem(nodeGates, wxT("Controlled Inverter"));

    tree->AppendItem(nodeWiring, wxT("Pin"));

    tree->ExpandAll();
}

void MainFrame::OnTreeLeftDown(wxMouseEvent& event)
{
    int flags = 0;
    wxTreeItemId item = m_treeProject->HitTest(event.GetPosition(), flags);

    if (item.IsOk()
        && (flags & wxTREE_HITTEST_ONITEM)
        && !m_treeProject->ItemHasChildren(item))
    {
        if (auto* custom = dynamic_cast<CustomTreeItem*>(m_treeProject->GetItemData(item)))
            m_canvas->AddCustomComponent(custom->definition);
        else if (item != m_customFolder)
            m_canvas->AddComponent(m_treeProject->GetItemText(item));
    }
    event.Skip();
}

void MainFrame::OnCreateCustom(wxCommandEvent& /*event*/)
{
    auto definition = m_canvas->PackageSelection();
    if (!definition) return;

    wxTextEntryDialog dialog(this, wxT("Name this component (1–24 characters). ")
        wxT("It will be available until the editor closes."),
        wxT("Create Custom Component"));
    while (dialog.ShowModal() == wxID_OK)
    {
        auto name = dialog.GetValue();
        name.Trim().Trim(false);
        bool duplicate = false;
        wxTreeItemIdValue cookie;
        for (auto item = m_treeProject->GetFirstChild(m_customFolder, cookie);
            item.IsOk(); item = m_treeProject->GetNextChild(m_customFolder, cookie))
        {
            if (m_treeProject->GetItemText(item).CmpNoCase(name) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (name.empty() || name.length() > 24 || duplicate)
        {
            wxMessageBox(duplicate ? wxT("A custom component already uses this name.")
                : wxT("Enter a name with 1–24 characters."),
                wxT("Create Custom Component"), wxOK | wxICON_WARNING, this);
            continue;
        }
        definition->name = name;
        m_treeProject->AppendItem(m_customFolder, name, -1, -1, new CustomTreeItem(definition));
        m_treeProject->Expand(m_customFolder);
        SetStatusText(wxT("Custom component added. Click it in the tree to place an instance."), 1);
        return;
    }
}

void MainFrame::OnExportNetlist(wxCommandEvent& /*event*/)
{
    ExportNetlist(false);
}

void MainFrame::OnExportKicadNetlist(wxCommandEvent& /*event*/)
{
    ExportNetlist(true);
}

void MainFrame::ExportNetlist(bool kicad)
{
    const wxString format = kicad ? wxT("KiCad") : wxT("Protel");
    const wxString defaultFile = kicad ? wxT("circuit_kicad.net") : wxT("circuit.net");
    const wxString wildcard = kicad
        ? wxT("KiCad netlist (*.net)|*.net|All files (*.*)|*.*")
        : wxT("Protel netlist (*.net)|*.net|All files (*.*)|*.*");

    wxFileDialog dialog(this, wxString(wxT("Export ")) + format + wxT(" Netlist"),
        wxEmptyString, defaultFile, wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (dialog.ShowModal() != wxID_OK)
        return;

    wxString path = dialog.GetPath();
    if (path.empty())
        return;
    if (wxFileName(path).GetExt().empty())
        path += wxT(".net");

    wxString error;
    const bool exported = kicad
        ? m_canvas->ExportKicadNetlist(path, error)
        : m_canvas->ExportProtelNetlist(path, error);

    if (!exported)
    {
        wxMessageBox(error, wxString(wxT("Export ")) + format + wxT(" Netlist"),
            wxOK | wxICON_ERROR, this);
        return;
    }

    SetStatusText(format + wxT(" netlist exported: ") + path, 1);
}

void MainFrame::OnToolPin(wxCommandEvent& /*event*/)
{
    m_canvas->AddComponent(wxT("Pin"));
}

// Select 与 Line 两个按钮互斥
void MainFrame::OnToolSelect(wxCommandEvent& /*event*/)
{
    bool checked = m_toolBar->GetToolState(ID_TOOL_SELECT);
    if (checked)
    {
        m_toolBar->ToggleTool(ID_TOOL_LINE, false);
        m_canvas->SetMode(CircuitCanvas::MODE_SELECT);
    }
    else
    {
        m_canvas->SetMode(CircuitCanvas::MODE_NORMAL);
    }
}

void MainFrame::OnToolLine(wxCommandEvent& /*event*/)
{
    bool checked = m_toolBar->GetToolState(ID_TOOL_LINE);
    if (checked)
    {
        m_toolBar->ToggleTool(ID_TOOL_SELECT, false);
        m_canvas->SetMode(CircuitCanvas::MODE_LINE);
    }
    else
    {
        m_canvas->SetMode(CircuitCanvas::MODE_NORMAL);
    }
}

void MainFrame::BuildPropertyPanel(wxWindow* /*parent*/)
{
    // 占位
}


bool CircuitEditorApp::OnInit()
{
    MainFrame* frame = new MainFrame(wxT("Circuit Editor: main of Untitled"));
    frame->Show(true);
    return true;
}