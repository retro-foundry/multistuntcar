(function(root) {
    'use strict';

    var actions = [
        { name: 'left', label: 'Steer left', sdl: 'dpleft', bit: 1 },
        { name: 'right', label: 'Steer right', sdl: 'dpright', bit: 2 },
        { name: 'accel', label: 'Accelerate / joystick up', sdl: 'dpup', bit: 4 },
        { name: 'brake', label: 'Brake / joystick down', sdl: 'dpdown', bit: 8 },
        { name: 'boost', label: 'Boost / fire / menu confirm', sdl: 'a', bit: 16 }
    ];
    var storageKey = 'stuntcar-controller-bindings-v1';

    function validProfile(profile) {
        return profile && actions.every(function(action) {
            return typeof profile[action.name] === 'string' && /^(b\d+|[+-]a\d+)$/.test(profile[action.name]);
        });
    }

    function bindingActive(binding, pad) {
        if (binding[0] === 'b') {
            var button = pad.buttons[Number(binding.slice(1))];
            return !!button && (button.pressed || button.value >= 0.5);
        }
        var axis = pad.axes[Number(binding.slice(2))];
        return typeof axis === 'number' && (binding[0] === '-' ? axis <= -0.5 : axis >= 0.5);
    }

    function inputForProfile(profile, pad) {
        return actions.reduce(function(input, action) {
            return input | (bindingActive(profile[action.name], pad) ? action.bit : 0);
        }, 0);
    }

    // SDL_CreateJoystickName() trims and collapses spaces in browser device IDs.
    function sdlName(id) { return id.trim().replace(/ +/g, ' '); }

    function mappingForProfile(profile, guid) {
        return guid + ',Custom browser joystick,' + actions.map(function(action) {
            return action.sdl + ':' + profile[action.name];
        }).join(',') + ',';
    }

    function detectBinding(previous, pad) {
        for (var i = 0; i < pad.buttons.length; ++i) {
            if (bindingActive('b' + i, pad) && !bindingActive('b' + i, previous)) return 'b' + i;
        }
        for (var j = 0; j < pad.axes.length; ++j) {
            for (var sign of ['-', '+']) {
                var binding = sign + 'a' + j;
                if (bindingActive(binding, pad) && !bindingActive(binding, previous)) return binding;
            }
        }
        return null;
    }

    var api = { actions: actions, validProfile: validProfile, inputForProfile: inputForProfile,
        mappingForProfile: mappingForProfile, detectBinding: detectBinding, sdlName: sdlName };
    if (typeof module === 'object' && module.exports) module.exports = api;
    if (!root.document) return;

    var panel = document.getElementById('controllerSettings');
    var device = document.getElementById('controllerDevice');
    var fields = document.getElementById('controllerBindings');
    var status = document.getElementById('controllerStatus');
    var readout = document.getElementById('controllerReadout');
    var profiles = Object.create(null);
    var selects = Object.create(null);
    var revision = 1;
    var deviceSignature = '';
    var learning = null;
    var storageError = '';

    try {
        var stored = JSON.parse(localStorage.getItem(storageKey) || '{}');
        if (!stored || typeof stored !== 'object' || Array.isArray(stored)) throw new Error('Invalid saved bindings');
        Object.keys(stored).forEach(function(id) {
            if (!validProfile(stored[id])) throw new Error('Invalid saved bindings for ' + id);
            profiles[id] = stored[id];
        });
    } catch (error) {
        storageError = 'Could not load saved controls: ' + error.message;
        status.textContent = storageError;
    }

    function pads() {
        return navigator.getGamepads ? Array.from(navigator.getGamepads()).filter(function(pad) {
            return pad && pad.connected;
        }) : [];
    }

    function selectedPad() {
        return pads().find(function(pad) { return String(pad.index) === device.value; });
    }

    function saveProfiles() {
        try {
            localStorage.setItem(storageKey, JSON.stringify(profiles));
            storageError = '';
            return true;
        } catch (error) {
            storageError = 'Controls work for this session but could not be saved: ' + error.message;
            status.textContent = storageError;
            return false;
        }
    }

    actions.forEach(function(action) {
        var label = document.createElement('label');
        label.textContent = action.label;
        var select = document.createElement('select');
        select.setAttribute('aria-label', action.label);
        selects[action.name] = select;
        var detect = document.createElement('button');
        detect.type = 'button';
        detect.textContent = 'Detect';
        detect.addEventListener('click', function() {
            var pad = selectedPad();
            if (!pad) return;
            learning = { action: action, index: pad.index, previous: snapshot(pad) };
            status.textContent = 'Release the controls, then use: ' + action.label + '. Select a binding to cancel.';
        });
        select.addEventListener('change', function() { learning = null; });
        label.appendChild(select);
        label.appendChild(detect);
        fields.appendChild(label);
    });

    function showDevice() {
        learning = null;
        var pad = selectedPad();
        actions.forEach(function(action) {
            var select = selects[action.name];
            select.replaceChildren(new Option('Choose an input', ''));
            if (!pad) return;
            pad.axes.forEach(function(_, index) {
                select.add(new Option('Axis ' + index + ' negative', '-a' + index));
                select.add(new Option('Axis ' + index + ' positive', '+a' + index));
            });
            pad.buttons.forEach(function(_, index) { select.add(new Option('Button ' + index, 'b' + index)); });
            if (profiles[pad.id]) select.value = profiles[pad.id][action.name];
        });
        status.textContent = storageError || (!pad ? 'Connect a controller and press a button to make it visible.' :
            profiles[pad.id] ? 'Custom controls are active. Left/right select menus; fire confirms.' :
            'Using the game\'s default controls. Assign all five actions, then apply.');
    }

    device.addEventListener('change', showDevice);
    panel.addEventListener('toggle', function() { if (!panel.open) learning = null; });
    document.getElementById('controllerApply').addEventListener('click', function() {
        var pad = selectedPad();
        if (!pad) return;
        var profile = {};
        actions.forEach(function(action) { profile[action.name] = selects[action.name].value; });
        if (!validProfile(profile)) {
            status.textContent = 'Choose an input for all five actions before applying.';
            return;
        }
        profiles[pad.id] = profile;
        ++revision;
        learning = null;
        if (saveProfiles()) status.textContent = 'Controls saved for this device. Close Controller setup to play.';
    });
    document.getElementById('controllerReset').addEventListener('click', function() {
        var pad = selectedPad();
        if (!pad) return;
        delete profiles[pad.id];
        // SDL has no remove-mapping API. Reload to restore its original mapping database.
        if (saveProfiles()) root.location.reload();
    });

    function snapshot(pad) {
        return { axes: Array.from(pad.axes), buttons: Array.from(pad.buttons).map(function(button) {
            return { pressed: button.pressed, value: button.value };
        }) };
    }

    function poll() {
        var connected = pads();
        var signature = JSON.stringify(connected.map(function(pad) {
            return [pad.index, pad.id, pad.axes.length, pad.buttons.length];
        }));
        if (signature !== deviceSignature) {
            var selected = device.value;
            deviceSignature = signature;
            device.replaceChildren();
            connected.forEach(function(pad) { device.add(new Option(pad.id, String(pad.index))); });
            if (connected.some(function(pad) { return String(pad.index) === selected; })) device.value = selected;
            showDevice();
        }
        if (panel.open) {
            var pad = selectedPad();
            readout.textContent = pad ? 'Mapping: ' + (pad.mapping || 'non-standard') + '\nAxes: ' +
                Array.from(pad.axes).map(function(value, index) { return index + ': ' + value.toFixed(2); }).join('  ') +
                '\nButtons down: ' + Array.from(pad.buttons).map(function(button, index) {
                    return button.pressed || button.value >= 0.5 ? index : null;
                }).filter(function(index) { return index !== null; }).join(', ') : 'No controller detected.';
            if (learning && pad && pad.index === learning.index) {
                var binding = detectBinding(learning.previous, pad);
                learning.previous = snapshot(pad);
                if (binding) {
                    selects[learning.action.name].value = binding;
                    status.textContent = learning.action.label + ' assigned. Apply controls when all five are set.';
                    learning = null;
                }
            }
        }
        requestAnimationFrame(poll);
    }

    root.StuntControllers = {
        revision: function() { return revision; },
        isConfiguring: function() { return panel.open; },
        mappingError: function(message) { status.textContent = 'Could not apply controller mapping: ' + message; },
        getSDLMapping: function(name, guid) {
            var pad = pads().find(function(candidate) { return sdlName(candidate.id) === name; });
            return pad && profiles[pad.id] ? mappingForProfile(profiles[pad.id], guid) : null;
        },
        // null preserves the existing standard-gamepad path for WebRTC guests.
        customInput: function(pad) {
            if (panel.open) return 0;
            return profiles[pad.id] ? inputForProfile(profiles[pad.id], pad) : null;
        }
    };
    showDevice();
    poll();
})(typeof window !== 'undefined' ? window : globalThis);
