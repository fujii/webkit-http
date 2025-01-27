window.onload = function ()
{
    if (!window.internals) {
        console.log("This test must be run in DumpRenderTree or WebKitTestRunner.");
        return;
    }
    let inputElements = document.getElementsByTagName("input");
    for (let inputElement of inputElements)
        internals.setShowAutoFillButton(inputElement, inputElement.dataset.autoFillButtonType);
    if (window.runTest)
        window.runTest();
}
