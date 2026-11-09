pragma Singleton
import QtQuick

// XMF desktop theme — "industrial amber" direction:
// warm paper/graphite surfaces, amber accent, dark section header bars,
// sharp 3px corners, denser rows. Light and dark variants.
QtObject {
    id: theme

    property bool darkMode: false

    // ---- motion -----------------------------------------------------------
    property bool animations: true
    readonly property int durFast: animations ? 90 : 0
    readonly property int durNormal: animations ? 140 : 0
    readonly property int durSlow: animations ? 200 : 0
    readonly property int easing: Easing.OutCubic
    readonly property int spring: Easing.OutCubic

    // ---- window / surfaces -------------------------------------------------
    readonly property color windowBg: darkMode ? "#1B1916" : "#E9E4DA"
    readonly property color windowTint: darkMode ? "#FF1B1916" : "#FFE9E4DA"
    readonly property color windowBorder: darkMode ? "#3E382F" : "#C7BDA9"

    readonly property color panel: darkMode ? "#24211D" : "#FAF7F0"
    readonly property color panelAlt: darkMode ? "#2C2823" : "#F1ECE2"
    readonly property color panelHover: darkMode ? "#3A342B" : "#F7EAD7"
    readonly property color toolbar: darkMode ? "#26221C" : "#F4EEE2"
    readonly property color sidebar: darkMode ? "#211F1A" : "#F0EBE1"

    readonly property color border: darkMode ? "#4A443B" : "#D3CAB9"
    readonly property color borderStrong: darkMode ? "#5D5547" : "#BEB29D"
    readonly property color divider: darkMode ? "#3A342B" : "#DED5C5"

    // ---- dark section header bars -----------------------------------------
    readonly property color sectionBg: darkMode ? "#332F28" : "#2B2A26"
    readonly property color sectionText: "#F5F0E6"
    readonly property color sectionMuted: darkMode ? "#C9BFAE" : "#B9AE9A"

    // ---- text --------------------------------------------------------------
    readonly property color text: darkMode ? "#F2ECE2" : "#24211C"
    readonly property color textSub: darkMode ? "#C9BFAE" : "#5C5446"
    readonly property color textMuted: darkMode ? "#9C9182" : "#8B8070"
    readonly property color textOnAccent: "#FFFFFF"

    // ---- accent (industrial amber; primary actions only) -------------------
    readonly property color accent: darkMode ? "#E8A33D" : "#D97706"
    readonly property color accentHover: darkMode ? "#F2B755" : "#B45309"
    readonly property color accentSoft: darkMode ? "#3A2E1E" : "#FBEFDC"
    readonly property color selection: darkMode ? "#46351F" : "#F3E0C4"

    // ---- status ------------------------------------------------------------
    readonly property color success: darkMode ? "#8BC34A" : "#3F7D3A"
    readonly property color warning: darkMode ? "#F0A35E" : "#C2410C"
    readonly property color danger: darkMode ? "#E57373" : "#B3261E"

    // ---- legacy aliases ----------------------------------------------------
    readonly property color accent2: darkMode ? "#3A342B" : "#E4D8C5"
    readonly property color bg0: windowBg
    readonly property color bg1: panel
    readonly property color glass: panel
    readonly property color glassHover: panelHover
    readonly property color glassStrong: panelAlt
    readonly property color glassHighlight: darkMode ? "#14FFFFFF" : "#FFF8EC"
    readonly property color borderAccent: accent
    readonly property color qcom: darkMode ? "#E8A33D" : "#B45309"
    readonly property color mtk: darkMode ? "#C9BFAE" : "#5C5446"
    readonly property color sprd: darkMode ? "#C9BFAE" : "#6B6152"
    readonly property color rk: darkMode ? "#E8A33D" : "#B45309"
    readonly property color aw: darkMode ? "#C9BFAE" : "#5C5446"

    // ---- geometry ----------------------------------------------------------
    readonly property int r22: 4
    readonly property int r20: 4
    readonly property int r18: 4
    readonly property int r16: 3
    readonly property int r14: 3
    readonly property int r12: 3
    readonly property int r10: 2
    readonly property int r6: 2
    readonly property int r8: 2
    readonly property int pill: 3
    readonly property int pagePadding: 16

    readonly property string fontFamily: "Microsoft YaHei UI"

    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a);
    }

    function lighten(c, amount) {
        return Qt.rgba(Math.min(1, c.r + amount), Math.min(1, c.g + amount), Math.min(1, c.b + amount), c.a);
    }

    function platformColor(id) {
        switch (id) {
        case "qcom": return qcom
        case "mtk": return mtk
        case "sprd": return sprd
        case "rk": return rk
        case "aw": return aw
        }
        return textSub
    }

    function fadeGradient(c) {
        return [ { position: 0.0, color: alpha(c, 0.10) }, { position: 1.0, color: alpha(c, 0.0) } ];
    }
}
