<?php
	plugin_header();

	$nc     =   (strpos($PAGE, '_x12_') > 0) ? 12 : (
				(strpos($PAGE, '_x24_') > 0) ? 24 : (
				(strpos($PAGE, '_x48_') > 0) ? 48 : 1
				) );
	$do     =   (strpos($PAGE, '_do') > 0);
?>

<p>This plugin implements <?= $nc ?>-instrument MIDI sample player with stereo input and stereo output. For each instrument there are up to eight samples available to play for different note velocities.
<?php if ($do) { ?>
Also each instrument has it's own stereo output that makes possible to record instrument outputs into individual tracks.
<?php } ?>
</p>

<h2>Import SFZ files feature</h2>

<p>The plugin allows to import the limited <a href="https://sfzformat.com/" target="_blank">SFZ files.</a>.
<p>All regions are sorted and grouped by the group label and the note number taken from the SFZ file as a single instrument.
If the number of instruments is more than <?= $nc ?> then all instruments above this count are ignored.</p>
<p>The import of embedded into SFZ audio files is also supported.</p>

<p>The supported list of opcodes:</p>
<ul>
<li><b>#define</b> - variable definitions;</li>
<li><b>#include</b> - file includes;</li>
<li><b>default_path</b> - the default path the samples are located;</li>
<li><b>note_offset</b>, <b>octave_offset</b>;</li>
<li><b>sample</b>, including the built-in samples specified with &lt;sample&gt; command;</li>
<li><b>group_label</b> - can be used to group instruments into a single instrument;</li>
<li><b>key</b> - specifies the MIDI note assigned to the instrument;</li>
<li><b>lokey</b>, <b>hikey</b>, <b>pitch_keycenter</b> - used to determine the MIDI note if the 'key' is not specified;</li>
<li><b>lovel</b>, <b>hivel</b> - for velocity;</li>
<li><b>lorand</b>, <b>hirand</b> - used as a velocity if lovel and hivel is not specified;</li>
<li><b>tune</b> - fine tuning;</li>
<li><b>pan</b> - panning;</li>
<li><b>volume</b> - volume correction.</li>
</ul>

<h2>Import Hydrogen kits feature</h2>

<p>The plugin allows to import Hydrogen drumkit files by selecting 'Import' &rarr; 'Hydrogen drumkit file...' menu item.
The file dialog allows to select the desired Hydrogen kit and import.</p>
<p>Additional to this feature, the <b>Override Hydrogen kits</b> feature is also provided and can be enabled via the 'User paths...' menu item
or the 'UI behavior' &rarr; 'Override Hydrogen kits' menu item.</p>
<p>The main idea of the feature is following.</p>
<p>Additionally to the usual Hydrogen drumkit installation paths, user can specify additional drumkit installation path
in the 'User paths' dialog. This will trigger automatic rescan of Hydrogen drumkits on each change of this parameter.
All found drumkits will be available in the 'Import' &rarr; 'Installed Hydrogen drumkits' menu.</p>
<p>User also can specify the 'Hydrogen drumkit override path' that will be used to search for configuration files that
will override the original Hydrogen drumkits.<p>
<p>If the 'Override Hydrogen kits' option is set, the behaviour of the plugin is the following.<p>
<p>When loading hydrogen drumkit via the 'Import' &rarr; 'Hydrogen drumkit file...' menu item or selecting one of
the drumkits listed in the 'Import' &rarr; 'Installed Hydrogen drumkits' menu, the plugin removes the base path of the
selected file if it matches one of the following paths:</p>
<ul>
	<li>Typical hydrogen drumkit system installation paths (only when importing from the list of installed drumkits):</li>
	<ul>
		<li>/usr/share/hydrogen</li>
		<li>/usr/local/share/hydrogen</li>
		<li>/opt/hydrogen</li>
		<li>/share/hydrogen</li>
	</ul>
	<li>Typical hydrogen drumkit user paths in user's home directory (only when importing from the list of installed drumkits):</li>
	<ul>
		<li>.hydrogen</li>
		<li>.h2</li>
		<li>.config/hydrogen</li>
		<li>.config/h2</li>
	</ul>
	<li>The 'User Hydrogen kits path' if it is specified.</li>
	<li>The 'Override user Hydrogen kits path' if it is specified.</li>
</ul>
<p>If the base path has been successfully removed, the plugin tries to search the file with the same relative path
but the different extension ('*.cfg') in the following directories:<p>
<ul>
	<li>The 'Override user Hydrogen kits' path if it is specified.</li>
	<li>The 'User Hydrogen kits' path if it is specified.</li>
</ul>
<p>If the corresponding configuration file was found, the plugin uses this file for loading drumkit settings instead of
the original Hydrogen drumkit file.</p>
<p>Also note that 'User Hydrogen kits path' and 'Override user Hydrogen kits path' allow to use system environment variables
for path substitutions. For example, the following value:</p>
<pre>
/path/to/kit/${KIT_NAME}
</pre>
<p>Will be interpreted as:</p>
<pre>
/path/to/kit/my_drumkit
</pre>
<p>If the system environment variable 'KIT_NAME' is set to 'my_drumkit'.</p>

<h2>Configuration</h2>

<p><b>Controls:</b></p>
<ul>
	<li><b>Bypass</b> - hot bypass switch, when turned on (led indicator is shining), the plugin does not affect the input signal.</li>
	<li><b>Working area</b> - combo box that allows to switch between instrument setup and instrument mixer.</li>
</ul>

<p><b>'Instrument mixer' section:</b></p>
<ul>
	<li><b>Enabled</b> - enables the corresponding instrument.</li>
	<li><b>Mix gain</b> - the volume of the instrument in the output mix.</li>
	<?php if ($do) { ?>
	<li><b>Direct Out</b> - enables the output of the instrument to the separate track.</li>
	<?php } ?>
	<li><b>Pan Left</b> - the panorama of the left channel of the corresponding instrument.</li>
	<li><b>Pan Right</b> - the panorama of the right channel of the corresponding instrument.</li>
	<li><b>MIDI #</b> - the MIDI number of the note associated with the corresponding instrument. Allows to change the number with mouse scroll or mouse double click.</li>
	<li><b>Note on</b> - indicates that the corresponding instrument has triggered the MIDI Note On event.</li>
	<li><b>Listen</b> - forces the corresponding instrument to trigger the Note On event.</li>
</ul>

<p><b>'Instrument' section:</b></p>
<ul>
	<li><b>Channel</b> - the MIDI channel to trigger notes by the selected instrument or all channels.</li>
	<li><b>Note</b> - the note and the octave of the note to trigger for the selected instrument.</li>
	<li><b>MIDI #</b> - the MIDI number of the note for the selected instrument.</li>
	<li><b>Group</b> - The group assigned to the instrument. The sample playback will be stopped for all
	instruments in the same group except the one's that has triggered the Note On event.</li>
	<li><b>Muting</b> - when enabled, turns off sample playback for selected instrument 
	when the Channel Control MIDI message is received.</li>
	<li><b>Note off</b> - when enabled, turns off sample playback for selected channel when the Note Off
	MIDI message is received. The sample fade-out time can be controlled by the corresponding knob. The behaviour is
	different, depending on the	loop settings and muting settings. If the loop is enabled for the sample, then
	receiving MIDI Note OFF event will just break the loop and play the rest of the sample if muting is not set.
	If the instrument muting is set, then it will fade-out the sample.</li>
	<li><b>Dynamics</b> - allows to randomize the output gain of the selected instrument.</li>
	<li><b>Time drifting</b> - allows to randomize the time delay between the MIDI Note On event and the start of the sample's playback for the selected instrument.</li>
</ul>
<p><b>'Samples' section:</b></p>
<ul>
	<li><b>Listen</b> - the button that forces the playback of the selected sample for the selected instrument</li>
	<li><b>Main tab</b> - the main control of the sample</li>
	<ul>
		<li><b>Reverse</b> - reverse the playback order of the sample (play sample from the end up to the beginning).</li>
		<li><b>Head cut</b> - the time to be cut from the beginning of the current sample for the selected instrument.</li>
		<li><b>Tail cut</b> - the time to be cut from the end of the current sample for the selected instrument.</li>
		<li><b>Fade in</b> - the time to be faded from the beginning of the current sample for the selected instrument.</li>
		<li><b>Fade out</b> - the time to be faded from the end of the current sample for the selected instrument.</li>
		<li><b>Makeup</b> - the makeup gain of the sample volume for the selected instrument.</li>
		<li><b>Pre-delay</b> - the time delay between the MIDI note has triggered and the start of the sample's playback for the selected instrument</li>
	</ul>
	<li><b>Pitch tab:</b> - control over pitch of the sample</li>
	<ul>
		<li><b>Pitch</b> - the relative sample pitch in semitones.</li>
		<li><b>Compensate</b> - enables the time compensation of the pitched sample to match the length of the original sample.</li>
		<ul>
			<li><b>Linear</b> - linear crossfade between chunks.</li>
			<li><b>Constant power</b> - constant power crossfade between chunks.</li>
		</ul>
		<li><b>Fade length</b> - the relative to the chunk size cross-fade length between sample chunks.</li>
		<li><b>Chunk size</b> - the maximum chunk size to use for pitch compensation.</li>
	</ul>
	<li><b>Stretch tab:</b> - control over stretching of the sample</li>
	<ul>
		<li><b>Stretch knob</b> - the additional time to stretch or collapse the selected region of the audio sample.</li>
		<li><b>Stretch button</b> - enables the stretching algorithm.</li>
		<ul>
			<li><b>Linear</b> - linear crossfade between chunks.</li>
			<li><b>Constant power</b> - constant power crossfade between chunks.</li>
		</ul>
		<li><b>Start</b> - the start position of the stretch region.</li>
		<li><b>End</b> - the end position of the stretch region.</li>
		<li><b>Fade length</b> - the relative to the chunk size cross-fade length between sample chunks.</li>
		<li><b>Chunk size</b> - the maximum chunk size to use for stretching.</li>
	</ul>
	<li><b>Loop tab:</b> - control over loop over the sample</li>
	<ul>
		<li><b>Loop button</b> - enables the loop feature.</li>
		<li><b>Loop mode</b> - combo box that allows to select loop mode:</li>
		<ul>
			<li><b>Simple: Direct</b> - the loop is always played in the direction from the beginning of the sample up to it's end.</li>
			<li><b>Simple: Reverse</b> - the loop is always played in the direction from the end of the sample up to it's beginning.</li>
			<li><b>PP: Half Direct</b> - ping-pong, the alternation of the direction playback. The loop starts to play in the direction from the start of the sample up to it's start. It is allowed to leave the playback loop at any time.</li>
			<li><b>PP: Half Reverse</b> - ping-pong, the alternation of the direction playback. The loop starts to play in the direction from the end of the sample up to it's beginning. It is allowed to leave the playback loop at any time.</li>
			<li><b>PP: Full Direct</b> - ping-pong, the alternation of the direction playback. The loop starts to play in the direction from the start of the sample up to it's start. It is allowed to leave the playback loop only when the playback is performed in the opposite to the initial direction.</li>
			<li><b>PP: Full Reverse</b> - ping-pong, the alternation of the direction playback. The loop starts to play in the direction from the end of the sample up to it's beginning. It is allowed to leave the playback loop only when the playback is performed in the opposite to the initial direction.</li>
			<li><b>PP: Smart Direct</b> - ping-pong, the alternation of the direction playback. The loop starts to play in the direction from the start of the sample up to it's start. It is allowed to leave the playback loop only when the playback is performed in direction from the beginning of the sample up to it's end.</li>
			<li><b>PP: Smart Reverse</b> - ping-pong, the alternation of the direction playback. The loop starts to play in the direction from the end of the sample up to it's beginning. It is allowed to leave the playback loop only when the playback is performed in direction from the beginning of the sample up to it's end.</li>
		</ul>
		<li><b>Crossfade selection</b> - allows to switch the cross-fade type between consequent parts of the sample.</li>
		<ul>
			<li><b>Linear</b> - linear crossfade between parts.</li>
			<li><b>Constant power</b> - constant power crossfade between parts.</li>
		</ul>		
		<li><b>Start</b> - the start position of the loop region.</li>
		<li><b>End</b> - the end position of the loop region.</li>
		<li><b>Fade length</b> - the maximum length of the crossfade between consequent parts of the sample.</li>
	</ul>
</ul>
<p><b>'Sample matrix' section:</b></p>
<ul>
	<li><b>Enabled</b> - enables/disables the playback of the corresponding sample for the selected instrument.</li>
	<li><b>Active</b> - indicates that the sample is loaded, enabled and ready for playback.</li>
	<li><b>Velocity</b> - the maximum velocity of the note the sample can trigger. Allows to set up velocity layers between different samples.</li>
	<li><b>Pan Left</b> - the panorama of the left audio channel of the corresponding sample.</li>
	<li><b>Pan Right</b> - the panorama of the right audio channel of the corresponding sample.</li>
	<li><b>Listen</b> - the button that forces the playback of the corresponding sample.</li>
	<li><b>Note on</b> - indicates that the playback event of the correponding sample has triggered.</li>
</ul>
<p><b>'Audio channel' section:</b></p>
<ul>
	<li><b>Muting</b> - when enabled, turns off any sample playback when the Channel Control MIDI message is received.</li>
	<li><b>Note off</b> - when enabled, turns off any sample playback when the Note Off MIDI message is received. 
	The sample fade-out time can be controlled by the corresponding knob.</li>
	<li><b>Dry amount</b> - the gain of the input signal passed to the audio inputs of the plugin.</li>
	<li><b>Wet amount</b> - the gain of the processed signal.</li>
	<li><b>Output gain</b> - the overall output gain of the plugin.</li>
	<li><b>Mute</b> - the button that forces any sample playback to turn off.</li>
</ul>
