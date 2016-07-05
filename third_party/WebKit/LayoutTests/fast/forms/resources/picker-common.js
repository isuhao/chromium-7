window.jsTestIsAsync = true;

var popupWindow = null;

var popupOpenCallback = null;

function popupOpenCallbackWrapper() {
    popupWindow.removeEventListener("didOpenPicker", popupOpenCallbackWrapper);
    // We need some delay.  Without it, testRunner.notifyDone() freezes.
    // See crbug.com/562311.
    setTimeout(popupOpenCallback, 20);
}

function waitUntilClosing(callback) {
    setTimeout(callback, 1);
}

function rootWindow() {
    var currentWindow = window;
    while (currentWindow !== currentWindow.parent) {
        currentWindow = currentWindow.parent;
    }
    return currentWindow;
}

// openPicker opens a picker UI for the following types:
// - menulist SELECT
// - INPUT color
// - INPUT date/datetime-local/month/week
//
// |callback| is called if we successfully open the picker UI. However it is
// called only for the following types:
// - menulist SELECT on Windows, Linux, and CrOS
// - INPUT color with DATALIST
// - INPUT date/datetime-local/month/week
function openPicker(element, callback, errorCallback) {
    rootWindow().moveTo(window.screenX, window.screenY);
    element.offsetTop; // Force to lay out
    element.focus();
    if (element.tagName === "SELECT") {
        eventSender.keyDown("downArrow", ["altKey"]);
    } else if (element.tagName === "INPUT") {
        if (element.type === "color") {
            eventSender.keyDown(" ");
        } else {
            eventSender.keyDown("downArrow", ["altKey"]);
        }
    }
    popupWindow = window.internals.pagePopupWindow;
    if (typeof callback === "function" && popupWindow)
        setPopupOpenCallback(callback);
    else if (typeof errorCallback === "function" && !popupWindow)
        errorCallback();
}

function clickToOpenPicker(x, y, callback, errorCallback) {
    rootWindow().moveTo(window.screenX, window.screenY);
    eventSender.mouseMoveTo(x, y);
    eventSender.mouseDown();
    eventSender.mouseUp();
    popupWindow = window.internals.pagePopupWindow;
    if (typeof callback === "function" && popupWindow)
        setPopupOpenCallback(callback);
    else if (typeof errorCallback === "function" && !popupWindow)
        errorCallback();
}

function setPopupOpenCallback(callback) {
    console.assert(popupWindow);
    popupOpenCallback = (function(callback) {
        // We need to move the window to the top left of available space
        // because the window will move back to (0, 0) when the
        // ShellViewMsg_SetTestConfiguration IPC arrives.
        rootWindow().moveTo(window.screenX, window.screenY);
        callback();
    }).bind(this, callback);
    try {
        popupWindow.addEventListener("didOpenPicker", popupOpenCallbackWrapper, false);
    } catch(e) {
        debug(e.name);
    }
}