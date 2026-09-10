/* ==========================================================================
   Daedalus playground.

   Plain ES modules-free JavaScript, no framework and no bundler -- the server
   sends Content-Security-Policy: script-src 'self', so there is nothing inline
   and nothing from a CDN. Everything dynamic goes through the DOM API or the
   CSSOM, both of which CSP allows.

   Every value that comes back from the API is inserted with textContent, never
   innerHTML, so a username or an audit detail cannot inject markup.
   ========================================================================== */
'use strict';

(function () {
  // --- tiny helpers -------------------------------------------------------

  const $ = (id) => document.getElementById(id);

  function element(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined && text !== null) node.textContent = String(text);
    return node;
  }

  function clear(node) {
    while (node.firstChild) node.removeChild(node.firstChild);
  }

  async function api(path, options) {
    const response = await fetch(path, Object.assign({
      headers: { 'Content-Type': 'application/json' },
      credentials: 'same-origin'
    }, options || {}));

    let payload = {};
    try {
      payload = await response.json();
    } catch (error) {
      payload = { error: 'the server sent a response that was not JSON' };
    }
    if (!response.ok) {
      const message = payload.error || payload.message || ('request failed: ' + response.status);
      const failure = new Error(message);
      failure.status = response.status;
      throw failure;
    }
    return payload;
  }

  const post = (path, body) => api(path, { method: 'POST', body: JSON.stringify(body) });

  function say(node, text, kind) {
    node.textContent = text;
    node.className = 'message' + (kind ? ' ' + kind : '') +
      (node.id === 'app-message' ? ' floating is-visible' : '');
    if (node.id === 'app-message') {
      window.clearTimeout(say.timer);
      say.timer = window.setTimeout(() => { node.className = 'message floating'; }, 3200);
    }
  }

  function stat(container, label, value, tone) {
    const box = element('div', 'stat');
    box.appendChild(element('span', 'stat-label', label));
    box.appendChild(element('span', 'stat-value' + (tone ? ' ' + tone : ''), value));
    container.appendChild(box);
  }

  function parseNumberList(text) {
    return text
      .split(/[\s,]+/)
      .filter((token) => token.length > 0)
      .map(Number)
      .filter((value) => Number.isFinite(value));
  }

  /** Deterministic-ish sample data for the four input shapes. */
  function generateValues(count, shape) {
    const values = [];
    for (let i = 0; i < count; i += 1) {
      if (shape === 'sorted') values.push(i + 1);
      else if (shape === 'reversed') values.push(count - i);
      else if (shape === 'duplicates') values.push(1 + Math.floor(Math.random() * 5));
      else values.push(1 + Math.floor(Math.random() * Math.max(count, 20)));
    }
    return values;
  }

  // --- session ------------------------------------------------------------

  const gate = $('gate');
  const app = $('app');
  const gateMessage = $('gate-message');
  const appMessage = $('app-message');
  let currentUser = null;

  function showApp(user) {
    currentUser = user;
    gate.classList.add('is-hidden');
    app.classList.remove('is-hidden');
    $('session-user').textContent = user.username + ' · ' + user.role;

    document.querySelectorAll('.is-admin').forEach((node) => {
      node.classList.toggle('is-hidden', user.role !== 'admin');
    });

    loadAlgorithmLists();
    resetSortingInputs();
    resetTreeInputs();
    loadGraphSample();
  }

  function showGate() {
    currentUser = null;
    app.classList.add('is-hidden');
    gate.classList.remove('is-hidden');
  }

  document.querySelectorAll('.tab').forEach((tab) => {
    tab.addEventListener('click', () => {
      document.querySelectorAll('.tab').forEach((other) => other.classList.remove('is-active'));
      document.querySelectorAll('.pane').forEach((pane) => pane.classList.remove('is-active'));
      tab.classList.add('is-active');
      $(tab.dataset.pane).classList.add('is-active');
      gateMessage.textContent = '';
    });
  });

  $('pane-login').addEventListener('submit', async (event) => {
    event.preventDefault();
    try {
      const result = await post('/api/auth/login', {
        username: $('login-username').value,
        password: $('login-password').value
      });
      showApp({ username: result.username, role: result.role });
    } catch (error) {
      say(gateMessage, error.message, 'error');
    }
  });

  $('pane-register').addEventListener('submit', async (event) => {
    event.preventDefault();
    try {
      const result = await post('/api/auth/register', {
        username: $('register-username').value,
        email: $('register-email').value,
        password: $('register-password').value
      });
      say(gateMessage, result.message + ' — you can sign in now.', 'ok');
      $('tab-login').click();
      $('login-username').value = $('register-username').value;
      $('login-password').focus();
    } catch (error) {
      say(gateMessage, error.message, 'error');
    }
  });

  $('logout').addEventListener('click', async () => {
    try { await post('/api/auth/logout', {}); } catch (error) { /* signing out always succeeds locally */ }
    showGate();
  });

  document.querySelectorAll('.nav-item').forEach((item) => {
    item.addEventListener('click', () => {
      document.querySelectorAll('.nav-item').forEach((other) => other.classList.remove('is-active'));
      document.querySelectorAll('.view').forEach((view) => view.classList.remove('is-active'));
      item.classList.add('is-active');
      $(item.dataset.view).classList.add('is-active');
      if (item.dataset.view === 'view-admin') refreshAdmin();
    });
  });

  // --- algorithm lists ----------------------------------------------------

  async function loadAlgorithmLists() {
    try {
      const lists = await api('/api/algorithms');
      fillSelect($('sort-algorithm'), lists.sorting, 'intro');
      fillSelect($('graph-algorithm'), lists.graph, 'bfs');
    } catch (error) {
      say(appMessage, 'could not load the algorithm list: ' + error.message, 'error');
    }
  }

  function fillSelect(select, values, preferred) {
    clear(select);
    (values || []).forEach((value) => {
      const option = element('option', null, value);
      option.value = value;
      if (value === preferred) option.selected = true;
      select.appendChild(option);
    });
  }

  // --- sorting ------------------------------------------------------------

  function resetSortingInputs() {
    const count = Math.max(2, Number($('sort-size').value) || 40);
    $('sort-values').value = generateValues(count, $('sort-shape').value).join(', ');
    drawBars(parseNumberList($('sort-values').value), false);
  }

  function drawBars(values, sorted) {
    const canvas = $('sort-bars');
    clear(canvas);
    if (values.length === 0 || values.length > 400) return;

    const highest = Math.max.apply(null, values.concat([1]));
    const lowest = Math.min.apply(null, values.concat([0]));
    const span = Math.max(highest - Math.min(0, lowest), 1);

    values.forEach((value) => {
      const bar = element('div', 'bar' + (sorted ? ' sorted' : ''));
      // CSSOM, not a style attribute: CSP blocks the latter.
      bar.style.height = Math.max(2, ((value - Math.min(0, lowest)) / span) * 100) + '%';
      bar.title = String(value);
      canvas.appendChild(bar);
    });
  }

  $('sort-generate').addEventListener('click', resetSortingInputs);
  $('sort-shape').addEventListener('change', resetSortingInputs);

  $('sort-run').addEventListener('click', async () => {
    const values = parseNumberList($('sort-values').value);
    if (values.length < 1) { say(appMessage, 'enter at least one number', 'error'); return; }

    const button = $('sort-run');
    button.disabled = true;
    try {
      const result = await post('/api/sort', {
        algorithm: $('sort-algorithm').value,
        values: values
      });

      $('sort-table-wrap').classList.add('is-hidden');
      drawBars(result.sorted, true);

      const stats = $('sort-stats');
      clear(stats);
      stat(stats, 'algorithm', result.algorithm);
      stat(stats, 'comparisons', result.comparisons.toLocaleString());
      stat(stats, 'swaps', result.swaps.toLocaleString());
      stat(stats, 'time', result.milliseconds.toFixed(3) + ' ms');
      stat(stats, 'stable', result.stable ? 'yes' : 'no', result.stable ? 'good' : null);
      stat(stats, 'in place', result.inPlace ? 'yes' : 'no');
      stat(stats, 'worst case', result.worstComplexity);

      $('sort-trace').textContent = (result.trace || []).join('\n') || '(no events recorded)';
      say(appMessage, 'sorted ' + result.sorted.length + ' values', 'ok');
    } catch (error) {
      say(appMessage, error.message, 'error');
    } finally {
      button.disabled = false;
    }
  });

  $('sort-compare').addEventListener('click', async () => {
    const values = parseNumberList($('sort-values').value);
    if (values.length < 1) { say(appMessage, 'enter at least one number', 'error'); return; }

    const button = $('sort-compare');
    button.disabled = true;
    const options = Array.from($('sort-algorithm').options).map((option) => option.value);
    const body = $('sort-table').querySelector('tbody');
    clear(body);

    try {
      const rows = [];
      for (const algorithm of options) {
        // Sequential on purpose: running them in parallel would have the
        // measurements fight each other for the same cores.
        const result = await post('/api/sort', { algorithm: algorithm, values: values });
        rows.push(result);
      }
      const fewest = Math.min.apply(null, rows.map((row) => row.comparisons || Infinity));

      rows.sort((a, b) => a.comparisons - b.comparisons).forEach((result) => {
        const row = element('tr');
        if (result.comparisons === fewest && fewest > 0) row.className = 'is-best';
        [result.algorithm,
         result.comparisons.toLocaleString(),
         result.swaps.toLocaleString(),
         result.milliseconds.toFixed(3),
         result.stable ? 'yes' : 'no',
         result.inPlace ? 'yes' : 'no',
         result.worstComplexity].forEach((cell) => row.appendChild(element('td', null, cell)));
        body.appendChild(row);
      });

      $('sort-table-wrap').classList.remove('is-hidden');
      say(appMessage, 'compared ' + rows.length + ' algorithms on the same input', 'ok');
    } catch (error) {
      say(appMessage, error.message, 'error');
    } finally {
      button.disabled = false;
    }
  });

  // --- trees --------------------------------------------------------------

  function resetTreeInputs() {
    const count = Math.max(1, Number($('tree-size').value) || 15);
    const shape = $('tree-shape').value;
    const values = [];
    if (shape === 'sorted') {
      for (let i = 1; i <= count; i += 1) values.push(i);
    } else {
      const pool = [];
      for (let i = 1; i <= count; i += 1) pool.push(i);
      while (pool.length > 0) pool.splice(Math.floor(Math.random() * pool.length), 1).forEach((v) => values.push(v));
    }
    $('tree-values').value = values.join(', ');
  }

  $('tree-generate').addEventListener('click', resetTreeInputs);
  $('tree-shape').addEventListener('change', resetTreeInputs);

  /** Rebuilds a drawable tree from the level-order and in-order traversals. */
  function buildShape(levelOrder, inOrder) {
    const position = new Map();
    inOrder.forEach((value, index) => position.set(value, index));

    function insert(node, value) {
      if (node === null) return { value: value, left: null, right: null };
      if (position.get(value) < position.get(node.value)) node.left = insert(node.left, value);
      else node.right = insert(node.right, value);
      return node;
    }

    let root = null;
    levelOrder.forEach((value) => { root = insert(root, value); });
    return root;
  }

  function drawTree(root, nodeCount) {
    const canvas = $('tree-canvas');
    clear(canvas);
    if (!root || nodeCount === 0) {
      canvas.appendChild(element('p', 'hint', 'nothing to draw'));
      return;
    }
    if (nodeCount > 63) {
      canvas.appendChild(element('p', 'hint',
        'too many nodes to draw legibly (' + nodeCount + '); the traversals and the ' +
        'server-rendered diagram below still show the whole tree.'));
      return;
    }

    const svgNamespace = 'http://www.w3.org/2000/svg';
    const layout = [];
    let column = 0;
    let depth = 0;

    (function walk(node, level) {
      if (!node) return;
      depth = Math.max(depth, level);
      walk(node.left, level + 1);
      layout.push({ value: node.value, x: column += 1, y: level, node: node });
      walk(node.right, level + 1);
    })(root, 0);

    const spacingX = 42;
    const spacingY = 62;
    const width = (column + 1) * spacingX;
    const height = (depth + 1) * spacingY + 30;

    const svg = document.createElementNS(svgNamespace, 'svg');
    svg.setAttribute('width', String(width));
    svg.setAttribute('height', String(height));
    svg.setAttribute('viewBox', '0 0 ' + width + ' ' + height);

    const coordinates = new Map();
    layout.forEach((item) => {
      coordinates.set(item.node, { x: item.x * spacingX, y: item.y * spacingY + 26 });
    });

    // Edges first so the circles paint over the line ends.
    layout.forEach((item) => {
      const from = coordinates.get(item.node);
      [item.node.left, item.node.right].forEach((child) => {
        if (!child) return;
        const to = coordinates.get(child);
        const line = document.createElementNS(svgNamespace, 'line');
        line.setAttribute('x1', String(from.x));
        line.setAttribute('y1', String(from.y));
        line.setAttribute('x2', String(to.x));
        line.setAttribute('y2', String(to.y));
        line.setAttribute('class', 'edge-line');
        svg.appendChild(line);
      });
    });

    layout.forEach((item) => {
      const at = coordinates.get(item.node);
      const circle = document.createElementNS(svgNamespace, 'circle');
      circle.setAttribute('cx', String(at.x));
      circle.setAttribute('cy', String(at.y));
      circle.setAttribute('r', '15');
      circle.setAttribute('class', 'node-circle');
      svg.appendChild(circle);

      const label = document.createElementNS(svgNamespace, 'text');
      label.setAttribute('x', String(at.x));
      label.setAttribute('y', String(at.y + 4));
      label.setAttribute('class', 'node-label');
      label.textContent = String(item.value);
      svg.appendChild(label);
    });

    canvas.appendChild(svg);
  }

  $('tree-run').addEventListener('click', async () => {
    const values = parseNumberList($('tree-values').value);
    if (values.length === 0) { say(appMessage, 'enter at least one key', 'error'); return; }

    const button = $('tree-run');
    button.disabled = true;
    try {
      const result = await post('/api/tree', { kind: $('tree-kind').value, values: values });

      const stats = $('tree-stats');
      clear(stats);
      stat(stats, 'structure', result.kind);
      stat(stats, 'keys', result.size);
      stat(stats, 'height', result.height);
      const ideal = Math.ceil(Math.log2(result.size + 1)) - 1;
      stat(stats, 'ideal height', ideal);
      stat(stats, 'balanced', result.balanced ? 'yes' : 'no', result.balanced ? 'good' : 'bad');
      stat(stats, 'minimum', result.minimum);
      stat(stats, 'maximum', result.maximum);

      const traversals = $('tree-traversals');
      clear(traversals);
      [['in-order', result.inOrder], ['pre-order', result.preOrder],
       ['post-order', result.postOrder], ['level-order', result.levelOrder]]
        .forEach(([name, sequence]) => {
          traversals.appendChild(element('dt', null, name));
          traversals.appendChild(element('dd', null, (sequence || []).join(' ')));
        });

      $('tree-diagram').textContent = result.diagram || '';
      drawTree(buildShape(result.levelOrder || [], result.inOrder || []), result.size);
      say(appMessage, result.kind + ': height ' + result.height + ' for ' + result.size + ' keys', 'ok');
    } catch (error) {
      say(appMessage, error.message, 'error');
    } finally {
      button.disabled = false;
    }
  });

  $('tree-compare').addEventListener('click', async () => {
    const values = parseNumberList($('tree-values').value);
    if (values.length === 0) { say(appMessage, 'enter at least one key', 'error'); return; }

    const button = $('tree-compare');
    button.disabled = true;
    const stats = $('tree-stats');
    clear(stats);
    try {
      for (const kind of ['bst', 'avl', 'redblack', 'splay', 'treap']) {
        const result = await post('/api/tree', { kind: kind, values: values });
        stat(stats, kind, 'h = ' + result.height, result.balanced ? 'good' : 'bad');
      }
      say(appMessage, 'compare the heights: sorted input is what separates them', 'ok');
    } catch (error) {
      say(appMessage, error.message, 'error');
    } finally {
      button.disabled = false;
    }
  });

  // --- graphs -------------------------------------------------------------

  function loadGraphSample() {
    $('graph-edges').value = [
      'A B 4', 'A H 8', 'B C 8', 'B H 11', 'C D 7', 'C I 2', 'C F 4',
      'D E 9', 'D F 14', 'E F 10', 'F G 2', 'G H 1', 'G I 6', 'H I 7'
    ].join('\n');
    $('graph-source').value = 'A';
  }

  $('graph-sample').addEventListener('click', loadGraphSample);

  function parseEdges(text) {
    return text.split('\n').map((line) => line.trim()).filter((line) => line.length > 0)
      .map((line) => {
        const parts = line.split(/[\s,]+/);
        return { from: parts[0], to: parts[1], weight: parts.length > 2 ? Number(parts[2]) : 1 };
      })
      .filter((edge) => edge.from && edge.to);
  }

  /** Circular layout: good enough to see structure, and it never overlaps. */
  function drawGraph(edges, directed, highlightVertices) {
    const canvas = $('graph-canvas');
    clear(canvas);

    const vertices = [];
    edges.forEach((edge) => {
      if (vertices.indexOf(edge.from) === -1) vertices.push(edge.from);
      if (vertices.indexOf(edge.to) === -1) vertices.push(edge.to);
    });
    if (vertices.length === 0 || vertices.length > 40) {
      canvas.appendChild(element('p', 'hint',
        vertices.length === 0 ? 'no edges to draw' : 'too many vertices to draw legibly'));
      return;
    }

    const svgNamespace = 'http://www.w3.org/2000/svg';
    const size = 420;
    const radius = size / 2 - 40;
    const centre = size / 2;
    const highlighted = new Set(highlightVertices || []);

    const svg = document.createElementNS(svgNamespace, 'svg');
    svg.setAttribute('width', String(size));
    svg.setAttribute('height', String(size));
    svg.setAttribute('viewBox', '0 0 ' + size + ' ' + size);

    const at = new Map();
    vertices.forEach((vertex, index) => {
      const angle = (index / vertices.length) * Math.PI * 2 - Math.PI / 2;
      at.set(vertex, { x: centre + radius * Math.cos(angle), y: centre + radius * Math.sin(angle) });
    });

    edges.forEach((edge) => {
      const from = at.get(edge.from);
      const to = at.get(edge.to);
      const line = document.createElementNS(svgNamespace, 'line');
      line.setAttribute('x1', String(from.x));
      line.setAttribute('y1', String(from.y));
      line.setAttribute('x2', String(to.x));
      line.setAttribute('y2', String(to.y));
      line.setAttribute('class', 'edge-line');
      svg.appendChild(line);

      const label = document.createElementNS(svgNamespace, 'text');
      label.setAttribute('x', String((from.x + to.x) / 2));
      label.setAttribute('y', String((from.y + to.y) / 2 - 3));
      label.setAttribute('class', 'edge-label');
      label.textContent = String(edge.weight) + (directed ? ' →' : '');
      svg.appendChild(label);
    });

    vertices.forEach((vertex) => {
      const point = at.get(vertex);
      const circle = document.createElementNS(svgNamespace, 'circle');
      circle.setAttribute('cx', String(point.x));
      circle.setAttribute('cy', String(point.y));
      circle.setAttribute('r', '17');
      circle.setAttribute('class', 'node-circle' + (highlighted.has(vertex) ? ' highlight' : ''));
      svg.appendChild(circle);

      const label = document.createElementNS(svgNamespace, 'text');
      label.setAttribute('x', String(point.x));
      label.setAttribute('y', String(point.y + 4));
      label.setAttribute('class', 'node-label' + (highlighted.has(vertex) ? ' inverted' : ''));
      label.textContent = vertex;
      svg.appendChild(label);
    });

    canvas.appendChild(svg);
  }

  $('graph-run').addEventListener('click', async () => {
    const edges = parseEdges($('graph-edges').value);
    if (edges.length === 0) { say(appMessage, 'add at least one edge', 'error'); return; }

    const button = $('graph-run');
    button.disabled = true;
    try {
      const directed = $('graph-directed').checked;
      const result = await post('/api/graph', {
        algorithm: $('graph-algorithm').value,
        directed: directed,
        source: $('graph-source').value,
        edges: edges
      });

      const stats = $('graph-stats');
      clear(stats);
      stat(stats, 'algorithm', result.algorithm);
      stat(stats, 'vertices', result.vertexCount);
      stat(stats, 'edges', result.edgeCount);
      if (result.totalWeight !== undefined) stat(stats, 'total weight', result.totalWeight);
      if (result.settled !== undefined) stat(stats, 'vertices settled', result.settled);
      if (result.negativeCycle !== undefined) {
        stat(stats, 'negative cycle', result.negativeCycle ? 'yes' : 'no',
             result.negativeCycle ? 'bad' : 'good');
      }
      if (result.acyclic !== undefined) {
        stat(stats, 'acyclic', result.acyclic ? 'yes' : 'no', result.acyclic ? 'good' : 'bad');
      }
      if (result.connected !== undefined) {
        stat(stats, 'connected', result.connected ? 'yes' : 'no', result.connected ? 'good' : 'bad');
      }

      const highlight = result.order || result.articulationPoints || [];
      drawGraph(edges, directed, highlight);

      // Show the raw payload minus the bookkeeping fields already in the tiles.
      const shown = Object.assign({}, result);
      ['algorithm', 'vertexCount', 'edgeCount', 'directed'].forEach((key) => delete shown[key]);
      $('graph-output').textContent = JSON.stringify(shown, null, 2);
      say(appMessage, result.algorithm + ' finished', 'ok');
    } catch (error) {
      say(appMessage, error.message, 'error');
    } finally {
      button.disabled = false;
    }
  });

  // --- strings ------------------------------------------------------------

  $('string-run').addEventListener('click', async () => {
    const button = $('string-run');
    button.disabled = true;
    try {
      const text = $('string-text').value;
      const result = await post('/api/strings', {
        algorithm: $('string-algorithm').value,
        text: text,
        pattern: $('string-pattern').value
      });

      const stats = $('string-stats');
      clear(stats);
      stat(stats, 'algorithm', result.algorithm);
      if (result.count !== undefined) stat(stats, 'matches', result.count);
      if (result.distance !== undefined) stat(stats, 'edit distance', result.distance);
      if (result.longestPalindrome !== undefined) {
        stat(stats, 'palindrome length', result.longestPalindrome.length);
      }

      const highlight = $('string-highlight');
      clear(highlight);
      if (Array.isArray(result.matches)) {
        renderMatches(highlight, text, result.matches, $('string-pattern').value.length);
      } else if (result.longestPalindrome) {
        const start = text.indexOf(result.longestPalindrome);
        renderMatches(highlight, text, start >= 0 ? [start] : [], result.longestPalindrome.length);
      } else {
        highlight.appendChild(element('span', null, text));
      }

      const shown = Object.assign({}, result);
      delete shown.algorithm;
      $('string-output').textContent = JSON.stringify(shown, null, 2);
      say(appMessage, result.algorithm + ' finished', 'ok');
    } catch (error) {
      say(appMessage, error.message, 'error');
    } finally {
      button.disabled = false;
    }
  });

  /** Highlights match spans using DOM nodes only -- never innerHTML. */
  function renderMatches(container, text, positions, length) {
    if (!positions.length || length <= 0) {
      container.appendChild(element('span', null, text));
      return;
    }
    const ordered = positions.slice().sort((a, b) => a - b);
    let cursor = 0;
    ordered.forEach((start) => {
      if (start < cursor) return;   // skip overlaps so the output stays readable
      container.appendChild(element('span', null, text.slice(cursor, start)));
      container.appendChild(element('mark', null, text.slice(start, start + length)));
      cursor = start + length;
    });
    container.appendChild(element('span', null, text.slice(cursor)));
  }

  // --- admin --------------------------------------------------------------

  async function refreshAdmin() {
    if (!currentUser || currentUser.role !== 'admin') return;
    try {
      const [users, audit] = await Promise.all([
        api('/api/admin/users'),
        api('/api/admin/audit')
      ]);

      const userBody = $('admin-users');
      clear(userBody);
      (users.users || []).forEach((user) => {
        const row = element('tr');
        [user.username, user.email, user.role, user.active ? 'yes' : 'no', user.failedAttempts]
          .forEach((cell) => row.appendChild(element('td', null, cell)));
        userBody.appendChild(row);
      });

      const auditBody = $('admin-audit');
      clear(auditBody);
      (audit.entries || []).slice().reverse().forEach((entry) => {
        const row = element('tr');
        [entry.action, entry.username, entry.client,
         entry.succeeded ? 'ok' : 'failed', entry.detail]
          .forEach((cell) => row.appendChild(element('td', null, cell)));
        auditBody.appendChild(row);
      });
    } catch (error) {
      say(appMessage, error.message, 'error');
    }
  }

  $('admin-refresh').addEventListener('click', refreshAdmin);

  // --- boot ---------------------------------------------------------------

  (async function start() {
    try {
      const health = await api('/api/health');
      $('server-version').textContent = 'v' + health.version;
    } catch (error) {
      /* the version badge is cosmetic; a failure here must not block sign-in */
    }
    try {
      const me = await api('/api/auth/me');
      showApp(me);   // an unexpired cookie means we are already signed in
    } catch (error) {
      showGate();
    }
  })();
})();
