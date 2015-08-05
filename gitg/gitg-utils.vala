/*
 *
 * Copyright (C) 2015 - Jesse van den Kieboom
 *
 * gitg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * gitg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gitg. If not, see <http://www.gnu.org/licenses/>.
 */

namespace Gitg
{

class Utils
{
	public static string replace_home_dir_with_tilde(File file)
	{
		var name = file.get_parse_name();
		var homedir = Environment.get_home_dir();

		if (homedir != null)
		{
			try
			{
				var hd = Filename.to_utf8(homedir, -1, null, null);

				if (hd == name)
				{
					name = "~/";
				}
				else
				{
					if (name.has_prefix(hd + "/"))
					{
						name = "~" + name[hd.length:name.length];
					}
				}
			} catch {}
		}

		return name;
	}
}

}

// ex:ts=4 noet
