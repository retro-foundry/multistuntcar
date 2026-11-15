const { test } = require('node:test');
const assert = require('node:assert/strict');
const controls = require('../package/controller-config.js');
const fs = require('node:fs');
const vm = require('node:vm');

const profile = { left: '-a0', right: '+a0', accel: '-a1', brake: '+a1', boost: 'b0' };
const neutral = { axes: [0, 0], buttons: [{ pressed: false, value: 0 }] };
const pad = (x, y, fire = false) => ({ axes: [x, y], buttons: [{ pressed: fire, value: Number(fire) }] });

test('two-axis, one-button Amiga adapter maps all directions and fire', () => {
    for (const [state, expected] of [
        [neutral, 0], [pad(-1, 0), 1], [pad(1, 0), 2], [pad(0, -1), 4],
        [pad(0, 1), 8], [pad(0, 0, true), 16], [pad(-1, -1, true), 21]
    ]) assert.equal(controls.inputForProfile(profile, state), expected);
});

test('explicit bindings support swapped or inverted axes and button-only directions', () => {
    const swapped = { left: '+a1', right: '-a1', accel: '+a0', brake: '-a0', boost: 'b0' };
    assert.equal(controls.inputForProfile(swapped, pad(1, 1, true)), 21);
    const buttons = { left: 'b4', right: 'b3', accel: 'b2', brake: 'b1', boost: 'b0' };
    assert.equal(controls.inputForProfile(buttons, {
        axes: [], buttons: [true, false, true, false, true].map(pressed => ({ pressed, value: Number(pressed) }))
    }), 21);
});

test('noise and absent inputs do not activate actions; half-travel does', () => {
    assert.equal(controls.inputForProfile(profile, pad(0.49, -0.49)), 0);
    assert.equal(controls.inputForProfile(profile, pad(0.5, -0.5)), 6);
    assert.equal(controls.inputForProfile(profile, { axes: [], buttons: [] }), 0);
});

test('binding detection ignores held inputs and learns the next direction or button', () => {
    assert.equal(controls.detectBinding(pad(-1, 0), pad(-1, 0)), null);
    assert.equal(controls.detectBinding(pad(-1, 0), neutral), null);
    assert.equal(controls.detectBinding(neutral, pad(1, 0)), '+a0');
    assert.equal(controls.detectBinding(neutral, pad(0, -1)), '-a1');
    assert.equal(controls.detectBinding(neutral, pad(0, 0, true)), 'b0');
});

test('incomplete or malformed saved profiles are rejected', () => {
    assert.ok(controls.validProfile(profile));
    for (const value of [null, {}, [], { ...profile, left: '' }, { ...profile, boost: 1 },
        { ...profile, boost: 'b-1' }, { ...profile, left: 'a0' }, { ...profile, left: 'b0,a:b1' }]) {
        assert.ok(!controls.validProfile(value));
    }
});

test('SDL mapping uses directional half axes and menu confirm on the fire button', () => {
    assert.equal(controls.mappingForProfile(profile, '0123456789abcdef0123456789abcdef'),
        '0123456789abcdef0123456789abcdef,Custom browser joystick,' +
        'dpleft:-a0,dpright:+a0,dpup:-a1,dpdown:+a1,a:b0,');
});

test('WebRTC uses the guest custom controls and suppresses raw inputs during setup', () => {
    const html = fs.readFileSync(require.resolve('../package/template.html'), 'utf8');
    const start = html.indexOf('            function btnPressed(');
    const end = html.indexOf('            function flushGuestInputToWasm()', start);
    assert.ok(start >= 0 && end > start);
    const context = vm.createContext({ guestInput: { keys: new Set(), gamepads: new Map() },
        KEY_P1_LEFT: 1, KEY_P1_RIGHT: 2, KEY_P1_ACCEL: 4, KEY_P1_BRAKE: 8, KEY_P1_BOOST: 16 });
    vm.runInContext(html.slice(start, end), context);
    const received = { input: 21, axes: [1, 1], buttons: [] };
    context.guestInput.gamepads.set(0, received);
    assert.equal(context.guestInputToPlayer2Dword(), 21);
    received.input = 0;
    assert.equal(context.guestInputToPlayer2Dword(), 0);
    received.input = null;
    assert.equal(context.guestInputToPlayer2Dword(), 2); // Existing standard mapping.
    received.input = 0xffff;
    assert.equal(context.guestInputToPlayer2Dword(), 31); // Restrict remote input to driving bits.
    context.guestInput.keys.add('ArrowUp');
    received.input = 16;
    assert.equal(context.guestInputToPlayer2Dword(), 20);
});
