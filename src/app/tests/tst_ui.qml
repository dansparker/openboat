import QtQuick
import QtTest
import OpenBoat

TestCase {
    id: test
    name: "UI"
    when: windowShown

    Component { id: mainComponent; Main {} }

    property var main: null

    function find(name) {
        const item = findChild(main, name);
        verify(item !== null, "no item " + name);
        return item;
    }

    function init() {
        main = createTemporaryObject(mainComponent, test);
        verify(main !== null);
        tryVerify(() => main.visible);
        waitForRendering(main.contentItem);
    }

    function test_side_buttons_open_one_page_at_a_time() {
        const ais = find("aisPage"), settingsPage = find("settingsPage"), nav = find("navPage");
        mouseClick(find("aisButton"));
        verify(ais.visible);
        mouseClick(find("setupButton"));
        verify(settingsPage.visible);
        verify(!ais.visible, "pages must not stack");
        mouseClick(find("routesButton"));
        verify(nav.visible);
        verify(!settingsPage.visible);
        mouseClick(find("routesButton"));
        verify(!nav.visible);
    }

    function test_route_editing_on_the_chart() {
        const chart = find("chartView");
        const before = routes.routes.length;
        chart.editing = true;
        chart.editRouteIndex = -1;
        chart.editPoints = [];
        mouseClick(chart, 200, 200);
        wait(600);
        mouseClick(chart, 500, 400);
        wait(600);
        compare(chart.editPoints.length, 2, "tap appends");
        // the (+) handle in the middle of the leg inserts a point there
        mouseClick(chart, 350, 300);
        compare(chart.editPoints.length, 3, "handle inserts");
        // hold on a point deletes it (pause first: a press right after a click is a double click)
        wait(600);
        mousePress(chart, 200, 200);
        wait(1200);
        mouseRelease(chart, 200, 200);
        compare(chart.editPoints.length, 2, "hold deletes");
        // drag moves a point
        const lat0 = chart.editPoints[0].lat;
        wait(600);
        mousePress(chart, 350, 300);
        for (let i = 1; i <= 10; ++i) mouseMove(chart, 350, 300 + i * 10);
        mouseRelease(chart, 350, 400);
        verify(chart.editPoints[0].lat < lat0, "drag moves the point south");
        mouseClick(find("editSave"));
        compare(routes.routes.length, before + 1);
        verify(!chart.editing);
        compare(routes.routes[before].points.length, 2);
    }

    function test_logbook_quick_entry() {
        const page = find("logbookPage");
        page.visible = true;
        const before = logbook.entries.length;
        // delegates of a Repeater: look them up in their grid
        const grid = find("quickGrid");
        let button = null;
        for (const c of grid.children) if (c.text === "Abgelegt") button = c;
        verify(button !== null, "no quick entry button");
        mouseClick(button);
        compare(logbook.entries.length, before + 1);
        compare(logbook.entries[0].text, "Abgelegt");
    }

    function test_no_ais_targets_message() {
        mouseClick(find("aisButton"));
        verify(find("aisPage").visible);
        compare(boat.aisTargets.length, 0);
    }
}
