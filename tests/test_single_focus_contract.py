from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HTML = (ROOT / "prototype/index.html").read_text()
CSS = (ROOT / "prototype/styles.css").read_text()
APP = (ROOT / "prototype/app.js").read_text()
README = (ROOT / "README.md").read_text()


def test_exclusive_primary_views():
    assert 'id="now-playing"' in HTML
    assert 'id="panel"' in HTML
    assert 'id="search-controls"' in HTML
    assert 'id="players-controls"' in HTML
    assert '$("now-playing").hidden = !isHome' in APP
    assert '$("panel").hidden = isHome' in APP
    assert 'switchView("Now Playing")' in APP
    assert 'id="home"' not in HTML
    assert HTML.count('id="panel-back"') == 1


def test_fixed_appliance_viewport_and_internal_scroll():
    assert "800px" in CSS and "480px" in CSS
    assert "html,body{margin:0;width:100%;height:100%;overflow:hidden}" in CSS
    assert "overflow-y:auto" in CSS
    assert "one focused screen" in README
    assert "800 × 480" in README
