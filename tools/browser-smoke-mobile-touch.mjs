// Mobile touch-control scenario for browser smoke. It stays separate from the
// desktop browser smoke path so the main smoke script remains compact.

/// Runs the mobile viewport touch-control smoke scenario.
export async function runMobileTouchSmoke(options) {
  const {
    browser,
    url,
    assertResponseHeaders,
    waitForBrowserFrame,
    assertNoBrowserFailures,
    readHud,
    assertHud,
    readDebugContract,
    assertDebugContract,
    saveScreenshot,
    assertPixelStats
  } = options;
  const page = await browser.newPage({
    viewport: { width: 390, height: 844 },
    deviceScaleFactor: 2,
    isMobile: true,
    hasTouch: true
  });
  const consoleMessages = [];

  page.on("console", (message) => {
    consoleMessages.push(`${message.type()}: ${message.text()}`);
  });
  page.on("pageerror", (error) => {
    consoleMessages.push(`pageerror: ${error.message}`);
  });

  try {
    const response = await page.goto(url, { waitUntil: "load" });
    assertResponseHeaders(response);
    await waitForBrowserFrame(page);
    assertNoBrowserFailures(consoleMessages);

    const firstHud = await readHud(page);
    assertHud(firstHud, "FIRST", consoleMessages);
    const firstDebug = await readDebugContract(page);
    assertDebugContract(firstDebug);
    const touchControls = await readTouchControlState(page);
    assertTouchControlsVisible(touchControls, consoleMessages);
    const image = await saveScreenshot(page, "browser-mobile-touch.png");
    assertPixelStats(image.pixelStats, "browser mobile touch", consoleMessages);

    const playerBeforeMove = await readPlayerPosition(page);
    const activePointer = await startMobileJoystickDrag(page);
    await page.waitForFunction((before) => {
      const position = window.__ofgDebug?.getPlayerPosition?.();
      if (position === undefined) {
        return false;
      }

      return Math.hypot(position.x - before.x, position.z - before.z) > 0.1;
    }, playerBeforeMove, { timeout: 5000 });
    const playerAfterMove = await readPlayerPosition(page);
    await endMobileJoystickDrag(page, activePointer);
    const movementDistance = horizontalDistance(playerBeforeMove, playerAfterMove);
    assertPlayerMoved(playerBeforeMove, playerAfterMove);

    await dragMobileLook(page);
    await page.locator("#touch-camera-toggle").tap();
    await page.waitForFunction(() => document.querySelector("#camera-mode")?.textContent === "THIRD");
    await waitForBrowserFrame(page);
    assertNoBrowserFailures(consoleMessages);
    const toggledHud = await readHud(page);
    assertHud(toggledHud, "THIRD", consoleMessages);

    return {
      firstHud,
      toggledHud,
      firstDebug,
      touchControls,
      playerBeforeMove,
      playerAfterMove,
      movementDistance,
      image,
      consoleMessages
    };
  } finally {
    await page.close();
  }
}

/// Reads whether the mobile touch controls are visible and measurable.
async function readTouchControlState(page) {
  return page.evaluate(() => {
    const root = document.querySelector("#touch-controls");
    const moveZone = document.querySelector("#touch-move-zone");
    const cameraToggle = document.querySelector("#touch-camera-toggle");
    if (root === null || moveZone === null || cameraToggle === null) {
      return { exists: false };
    }

    const rootStyle = getComputedStyle(root);
    const moveRect = moveZone.getBoundingClientRect();
    const toggleRect = cameraToggle.getBoundingClientRect();
    return {
      exists: true,
      display: rootStyle.display,
      visibility: rootStyle.visibility,
      opacity: Number.parseFloat(rootStyle.opacity),
      moveZone: {
        left: moveRect.left,
        top: moveRect.top,
        width: moveRect.width,
        height: moveRect.height
      },
      cameraToggle: {
        left: toggleRect.left,
        top: toggleRect.top,
        width: toggleRect.width,
        height: toggleRect.height
      }
    };
  });
}

/// Reads the Rust-owned player position through the browser debug hook.
async function readPlayerPosition(page) {
  const position = await page.evaluate(() => window.__ofgDebug?.getPlayerPosition?.());
  if (
    position === undefined ||
    !Number.isFinite(position.x) ||
    !Number.isFinite(position.y) ||
    !Number.isFinite(position.z)
  ) {
    throw new Error(`Player position is unavailable or invalid: ${JSON.stringify(position)}`);
  }

  return position;
}

/// Validates that the mobile touch overlay is visible and sized.
function assertTouchControlsVisible(touchControls, consoleMessages) {
  if (
    !touchControls.exists ||
    touchControls.display === "none" ||
    touchControls.visibility === "hidden" ||
    touchControls.opacity <= 0 ||
    touchControls.moveZone.width <= 0 ||
    touchControls.moveZone.height <= 0 ||
    touchControls.cameraToggle.width <= 0 ||
    touchControls.cameraToggle.height <= 0
  ) {
    throw new Error(
      `Touch controls are not visible in mobile viewport: ${JSON.stringify(touchControls)} ` +
      `console=${JSON.stringify(consoleMessages)}`
    );
  }
}

/// Validates that a simulated touch joystick drag moved the Rust-owned player.
function assertPlayerMoved(before, after) {
  const distance = horizontalDistance(before, after);
  if (distance <= 0.1) {
    throw new Error(
      `Expected mobile joystick drag to move player, saw distance=${distance}. ` +
      `before=${JSON.stringify(before)} after=${JSON.stringify(after)}`
    );
  }
}

/// Starts a synthetic touch drag on the mobile joystick and leaves it active.
async function startMobileJoystickDrag(page) {
  const activePointer = await page.evaluate(() => {
    const zone = document.querySelector("#touch-move-zone");
    if (zone === null) {
      throw new Error("Missing #touch-move-zone.");
    }

    const rect = zone.getBoundingClientRect();
    const pointerId = 41;
    const startX = rect.left + rect.width * 0.5;
    const startY = rect.top + rect.height * 0.58;
    return {
      pointerId,
      startX,
      startY,
      x: startX,
      y: startY - 54
    };
  });
  await dispatchTouchPointer(page, "#touch-move-zone", "pointerdown", {
    pointerId: activePointer.pointerId,
    clientX: activePointer.startX,
    clientY: activePointer.startY
  });
  await dispatchTouchPointer(page, "#touch-move-zone", "pointermove", {
    pointerId: activePointer.pointerId,
    clientX: activePointer.x,
    clientY: activePointer.y
  });
  return {
    pointerId: activePointer.pointerId,
    x: activePointer.x,
    y: activePointer.y
  };
}

/// Ends the active synthetic mobile joystick drag.
async function endMobileJoystickDrag(page, activePointer) {
  await dispatchTouchPointer(page, "#touch-move-zone", "pointerup", {
    pointerId: activePointer.pointerId,
    clientX: activePointer.x,
    clientY: activePointer.y
  });
}

/// Sends a short synthetic look drag through the right-side touch area.
async function dragMobileLook(page) {
  const drag = await page.evaluate(() => {
    const zone = document.querySelector("#touch-look-zone");
    if (zone === null) {
      throw new Error("Missing #touch-look-zone.");
    }

    const rect = zone.getBoundingClientRect();
    const pointerId = 42;
    const startX = rect.left + rect.width * 0.5;
    const startY = rect.top + rect.height * 0.5;
    return {
      pointerId,
      startX,
      startY,
      endX: startX + 36,
      endY: startY - 18
    };
  });
  await dispatchTouchPointer(page, "#touch-look-zone", "pointerdown", {
    pointerId: drag.pointerId,
    clientX: drag.startX,
    clientY: drag.startY
  });
  await dispatchTouchPointer(page, "#touch-look-zone", "pointermove", {
    pointerId: drag.pointerId,
    clientX: drag.endX,
    clientY: drag.endY
  });
  await dispatchTouchPointer(page, "#touch-look-zone", "pointerup", {
    pointerId: drag.pointerId,
    clientX: drag.endX,
    clientY: drag.endY
  });
}

/// Dispatches one touch-flavored PointerEvent in the browser page.
async function dispatchTouchPointer(page, selector, type, pointer) {
  await page.evaluate(({ selector, type, pointer }) => {
    const target = document.querySelector(selector);
    if (target === null) {
      throw new Error(`Missing ${selector}.`);
    }

    target.dispatchEvent(new PointerEvent(type, {
      bubbles: true,
      cancelable: true,
      composed: true,
      pointerId: pointer.pointerId,
      pointerType: "touch",
      isPrimary: true,
      clientX: pointer.clientX,
      clientY: pointer.clientY,
      button: 0
    }));
  }, { selector, type, pointer });
}

/// Computes horizontal movement between two player positions.
function horizontalDistance(a, b) {
  return Math.hypot(a.x - b.x, a.z - b.z);
}
