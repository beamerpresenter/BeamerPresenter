# SPDX-FileCopyrightText: 2022 Valentin Bruch <software@vbruch.eu>
# SPDX-License-Identifier: GPL-3.0-or-later OR AGPL-3.0-or-later

if (USE_MUPDF)
  set(HTML_LICENSE_STRING "<p>This software may be redistributed and/or modified under the terms of the GNU Affero General Public License (AGPL), version 3, available on the <a href=\"https://www.gnu.org/licenses/agpl-3.0.html\">GNU website</a>, or (at your choice) any newer version of the same license. You can find more details about the license in the LICENSE file, which you should have obtained with this software, and which is also available <a href=\"https://github.com/beamerpresenter/BeamerPresenter/blob/main/LICENSE\">on github</a>.</p>")
else()
  set(HTML_LICENSE_STRING "<p>Every version of this software may be redistributed and/or modified under the terms of the <a href=\"https://www.gnu.org/licenses/agpl-3.0.html\">GNU Affero General Public License (AGPL)</a>, version 3 or (at your choice) any newer version of the same license.</p><p>This version of BeamerPresenter, compiled without MuPDF, may also be redistributed and/or modified under the terms of the GNU General Public License (GPL), version 3, available on the <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">GNU website</a>, or (at your choice) any newer version of the same license. You can find more details about the license in the LICENSE file, which you should have obtained with this software, and which is also available <a href=\"https://github.com/beamerpresenter/BeamerPresenter/blob/main/LICENSE\">on github</a>.</p>")
endif()

configure_file(doc/README.html.in doc/README.html @ONLY)
install(FILES
    "${PROJECT_BINARY_DIR}/share/doc/README.html"
    DESTINATION "${DOC_PATH}"
    OPTIONAL)
configure_file(applications/beamerpresenter.desktop.in applications/beamerpresenter.desktop @ONLY)
install(FILES
    "${PROJECT_BINARY_DIR}/share/applications/beamerpresenter.desktop"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
    RENAME "${APPID_NAME}.desktop"
    OPTIONAL)
install(FILES
    icons/beamerpresenter.svg
    DESTINATION "${APPICON_PATH}"
    RENAME "${APPID_NAME}.svg"
    OPTIONAL)
install(DIRECTORY
    icons/tools
    icons/actions
    icons/devices
    DESTINATION "${DEFAULT_ICON_PATH}")
