Polymer({
      is: 'app-drawer',

      properties: {
        /**
         * The opened state of the drawer.
         */
        opened: {
          type: Boolean,
          value: false,
          notify: true,
          reflectToAttribute: true
        },

        /**
         * The drawer does not have a scrim and cannot be swiped close.
         */
        persistent: {
          type: Boolean,
          value: false,
          reflectToAttribute: true
        },

        /**
         * The alignment of the drawer on the screen ('left', 'right', 'start' or 'end').
         * 'start' computes to left and 'end' to right in LTR layout and vice versa in RTL
         * layout.
         */
        align: {
          type: String,
          value: 'left'
        },

        /**
         * The computed, read-only position of the drawer on the screen ('left' or 'right').
         */
        position: {
          type: String,
          readOnly: true,
          value: 'left',
          reflectToAttribute: true
        },

        /**
         * Create an area at the edge of the screen to swipe open the drawer.
         */
        swipeOpen: {
          type: Boolean,
          value: false,
          reflectToAttribute: true
        },

        /**
         * Trap keyboard focus when the drawer is opened and not persistent.
         */
        noFocusTrap: {
          type: Boolean,
          value: false
        }
      },

      observers: [
        'resetLayout(position)',
        '_resetPosition(align, isAttached)'
      ],

      _translateOffset: 0,

      _trackDetails: null,

      _drawerState: 0,

      _boundEscKeydownHandler: null,

      _firstTabStop: null,

      _lastTabStop: null,

      ready: function() {
        // Set the scroll direction so you can vertically scroll inside the drawer.
        this.setScrollDirection('y');

        // Only transition the drawer after its first render (e.g. app-drawer-layout
        // may need to set the initial opened state which should not be transitioned).
        this._setTransitionDuration('0s');
      },

      attached: function() {
        // Only transition the drawer after its first render (e.g. app-drawer-layout
        // may need to set the initial opened state which should not be transitioned).
        Polymer.RenderStatus.afterNextRender(this, function() {
          this._setTransitionDuration('');
          this._boundEscKeydownHandler = this._escKeydownHandler.bind(this);
          this._resetDrawerState();

          this.listen(this, 'track', '_track');
          this.addEventListener('transitionend', this._transitionend.bind(this));
          this.addEventListener('keydown', this._tabKeydownHandler.bind(this))
        });
      },

      detached: function() {
        document.removeEventListener('keydown', this._boundEscKeydownHandler);
      },

      /**
       * Opens the drawer.
       */
      open: function() {
        this.opened = true;
      },

      /**
       * Closes the drawer.
       */
      close: function() {
        this.opened = false;
      },

      /**
       * Toggles the drawer open and close.
       */
      toggle: function() {
        this.opened = !this.opened;
      },

      /**
       * Gets the width of the drawer.
       *
       * @return {number} The width of the drawer in pixels.
       */
      getWidth: function() {
        return this.$.contentContainer.offsetWidth;
      },

      /**
       * Resets the layout. If you changed the size of app-header via CSS
       * you can notify the changes by either firing the `iron-resize` event
       * or calling `resetLayout` directly.
       *
       * @method resetLayout
       */
      resetLayout: function() {
        this.debounce('_resetLayout', function() {
          this.fire('app-drawer-reset-layout');
        }, 1);
      },

      _isRTL: function() {
        return window.getComputedStyle(this).direction === 'rtl';
      },

      _resetPosition: function() {
        switch (this.align) {
          case 'start':
            this._setPosition(this._isRTL() ? 'right' : 'left');
            return;
          case 'end':
            this._setPosition(this._isRTL() ? 'left' : 'right');
            return;
        }
        this._setPosition(this.align);
      },

      _escKeydownHandler: function(event) {
        var ESC_KEYCODE = 27;
        if (event.keyCode === ESC_KEYCODE) {
          // Prevent any side effects if app-drawer closes.
          event.preventDefault();
          this.close();
        }
      },

      _track: function(event) {
        if (this.persistent) {
          return;
        }

        // Disable user selection on desktop.
        event.preventDefault();

        switch (event.detail.state) {
          case 'start':
            this._trackStart(event);
            break;
          case 'track':
            this._trackMove(event);
            break;
          case 'end':
            this._trackEnd(event);
            break;
        }
      },

      _trackStart: function(event) {
        this._drawerState = this._DRAWER_STATE.TRACKING;

        // Disable transitions since style attributes will reflect user track events.
        this._setTransitionDuration('0s');
        this.style.visibility = 'visible';

        var rect = this.$.contentContainer.getBoundingClientRect();
        if (this.position === 'left') {
          this._translateOffset = rect.left;
        } else {
          this._translateOffset = rect.right - window.innerWidth;
        }

        this._trackDetails = [];
      },

      _trackMove: function(event) {
        this._translateDrawer(event.detail.dx + this._translateOffset);

        // Use Date.now() since event.timeStamp is inconsistent across browsers (e.g. most
        // browsers use milliseconds but FF 44 uses microseconds).
        this._trackDetails.push({
          dx: event.detail.dx,
          timeStamp: Date.now()
        });
      },

      _trackEnd: function(event) {
        var x = event.detail.dx + this._translateOffset;
        var drawerWidth = this.getWidth();
        var isPositionLeft = this.position === 'left';
        var isInEndState = isPositionLeft ? (x >= 0 || x <= -drawerWidth) :
          (x <= 0 || x >= drawerWidth);

        if (!isInEndState) {
          // No longer need the track events after this method returns - allow them to be GC'd.
          var trackDetails = this._trackDetails;
          this._trackDetails = null;

          this._flingDrawer(event, trackDetails);
          if (this._drawerState === this._DRAWER_STATE.FLINGING) {
            return;
          }
        }

        // If the drawer is not flinging, toggle the opened state based on the position of
        // the drawer.
        var halfWidth = drawerWidth / 2;
        if (event.detail.dx < -halfWidth) {
          this.opened = this.position === 'right';
        } else if (event.detail.dx > halfWidth) {
          this.opened = this.position === 'left';
        }

        // Trigger app-drawer-transitioned now since there will be no transitionend event.
        if (isInEndState) {
          this._resetDrawerState();
        }

        this._setTransitionDuration('');
        this._resetDrawerTranslate();
        this.style.visibility = '';
      },

      _calculateVelocity: function(event, trackDetails) {
        // Find the oldest track event that is within 100ms using binary search.
        var now = Date.now();
        var timeLowerBound = now - 100;
        var trackDetail;
        var min = 0;
        var max = trackDetails.length - 1;

        while (min <= max) {
          // Floor of average of min and max.
          var mid = (min + max) >> 1;
          var d = trackDetails[mid];
          if (d.timeStamp >= timeLowerBound) {
            trackDetail = d;
            max = mid - 1;
          } else {
            min = mid + 1;
          }
        }

        if (trackDetail) {
          var dx = event.detail.dx - trackDetail.dx;
          var dt = (now - trackDetail.timeStamp) || 1;
          return dx / dt;
        }
        return 0;
      },

      _flingDrawer: function(event, trackDetails) {
        var velocity = this._calculateVelocity(event, trackDetails);

        // Do not fling if velocity is not above a threshold.
        if (Math.abs(velocity) < this._MIN_FLING_THRESHOLD) {
          return;
        }

        this._drawerState = this._DRAWER_STATE.FLINGING;

        var x = event.detail.dx + this._translateOffset;
        var drawerWidth = this.getWidth();
        var isPositionLeft = this.position === 'left';
        var isVelocityPositive = velocity > 0;
        var isClosingLeft = !isVelocityPositive && isPositionLeft;
        var isClosingRight = isVelocityPositive && !isPositionLeft;
        var dx;
        if (isClosingLeft) {
          dx = -(x + drawerWidth);
        } else if (isClosingRight) {
          dx = (drawerWidth - x);
        } else {
          dx = -x;
        }

        // Enforce a minimum transition velocity to make the drawer feel snappy.
        if (isVelocityPositive) {
          velocity = Math.max(velocity, this._MIN_TRANSITION_VELOCITY);
          this.opened = this.position === 'left';
        } else {
          velocity = Math.min(velocity, -this._MIN_TRANSITION_VELOCITY);
          this.opened = this.position === 'right';
        }

        // Calculate the amount of time needed to finish the transition based on the
        // initial slope of the timing function.
        this._setTransitionDuration((this._FLING_INITIAL_SLOPE * dx / velocity) + 'ms');
        this._setTransitionTimingFunction(this._FLING_TIMING_FUNCTION);

        this._resetDrawerTranslate();
      },

      _transitionend: function(event) {
        // contentContainer will transition on opened state changed, and scrim will
        // transition on persistent state changed when opened - these are the
        // transitions we are interested in.
        var target = Polymer.dom(event).rootTarget;
        if (target === this.$.contentContainer || target === this.$.scrim) {

          // If the drawer was flinging, we need to reset the style attributes.
          if (this._drawerState === this._DRAWER_STATE.FLINGING) {
            this._setTransitionDuration('');
            this._setTransitionTimingFunction('');
            this.style.visibility = '';
          }

          this._resetDrawerState();
        }
      },

      _setTransitionDuration: function(duration) {
        this.$.contentContainer.style.transitionDuration = duration;
        this.$.scrim.style.transitionDuration = duration;
      },

      _setTransitionTimingFunction: function(timingFunction) {
        this.$.contentContainer.style.transitionTimingFunction = timingFunction;
        this.$.scrim.style.transitionTimingFunction = timingFunction;
      },

      _translateDrawer: function(x) {
        var drawerWidth = this.getWidth();

        if (this.position === 'left') {
          x = Math.max(-drawerWidth, Math.min(x, 0));
          this.$.scrim.style.opacity = 1 + x / drawerWidth;
        } else {
          x = Math.max(0, Math.min(x, drawerWidth));
          this.$.scrim.style.opacity = 1 - x / drawerWidth;
        }

        this.translate3d(x + 'px', '0', '0', this.$.contentContainer);
      },

      _resetDrawerTranslate: function() {
        this.$.scrim.style.opacity = '';
        this.transform('', this.$.contentContainer);
      },

      _resetDrawerState: function() {
        var oldState = this._drawerState;
        if (this.opened) {
          this._drawerState = this.persistent ?
            this._DRAWER_STATE.OPENED_PERSISTENT : this._DRAWER_STATE.OPENED;
        } else {
          this._drawerState = this._DRAWER_STATE.CLOSED;
        }

        if (oldState !== this._drawerState) {
          if (this._drawerState === this._DRAWER_STATE.OPENED) {
            this._setKeyboardFocusTrap();
            document.addEventListener('keydown', this._boundEscKeydownHandler);
            document.body.style.overflow = 'hidden';
          } else {
            document.removeEventListener('keydown', this._boundEscKeydownHandler);
            document.body.style.overflow = '';
          }

          // Don't fire the event on initial load.
          if (oldState !== this._DRAWER_STATE.INIT) {
            this.fire('app-drawer-transitioned');
          }
        }
      },

      _setKeyboardFocusTrap: function() {
        if (this.noFocusTrap) {
          return;
        }

        // NOTE: Unless we use /deep/ (which we shouldn't since it's deprecated), this will
        // not select focusable elements inside shadow roots.
        var focusableElementsSelector = [
            'a[href]:not([tabindex="-1"])',
            'area[href]:not([tabindex="-1"])',
            'input:not([disabled]):not([tabindex="-1"])',
            'select:not([disabled]):not([tabindex="-1"])',
            'textarea:not([disabled]):not([tabindex="-1"])',
            'button:not([disabled]):not([tabindex="-1"])',
            'iframe:not([tabindex="-1"])',
            '[tabindex]:not([tabindex="-1"])',
            '[contentEditable=true]:not([tabindex="-1"])'
          ].join(',');
        var focusableElements = Polymer.dom(this).querySelectorAll(focusableElementsSelector);

        if (focusableElements.length > 0) {
          this._firstTabStop = focusableElements[0];
          this._lastTabStop = focusableElements[focusableElements.length - 1];
        } else {
          // Reset saved tab stops when there are no focusable elements in the drawer.
          this._firstTabStop = null;
          this._lastTabStop = null;
        }

        // Focus on app-drawer if it has non-zero tabindex. Otherwise, focus the first focusable
        // element in the drawer, if it exists. Use the tabindex attribute since the this.tabIndex
        // property in IE/Edge returns 0 (instead of -1) when the attribute is not set.
        var tabindex = this.getAttribute('tabindex');
        if (tabindex && parseInt(tabindex, 10) > -1) {
          this.focus();
        } else if (this._firstTabStop) {
          this._firstTabStop.focus();
        }
      },

      _tabKeydownHandler: function(event) {
        if (this.noFocusTrap) {
          return;
        }

        var TAB_KEYCODE = 9;
        if (this._drawerState === this._DRAWER_STATE.OPENED && event.keyCode === TAB_KEYCODE) {
          if (event.shiftKey) {
            if (this._firstTabStop && Polymer.dom(event).localTarget === this._firstTabStop) {
              event.preventDefault();
              this._lastTabStop.focus();
            }
          } else {
            if (this._lastTabStop && Polymer.dom(event).localTarget === this._lastTabStop) {
              event.preventDefault();
              this._firstTabStop.focus();
            }
          }
        }
      },

      _MIN_FLING_THRESHOLD: 0.2,

      _MIN_TRANSITION_VELOCITY: 1.2,

      _FLING_TIMING_FUNCTION: 'cubic-bezier(0.667, 1, 0.667, 1)',

      _FLING_INITIAL_SLOPE: 1.5,

      _DRAWER_STATE: {
        INIT: 0,
        OPENED: 1,
        OPENED_PERSISTENT: 2,
        CLOSED: 3,
        TRACKING: 4,
        FLINGING: 5
      }

      /**
       * Fired when the layout of app-drawer has changed.
       *
       * @event app-drawer-reset-layout
       */

      /**
       * Fired when app-drawer has finished transitioning.
       *
       * @event app-drawer-transitioned
       */
    });