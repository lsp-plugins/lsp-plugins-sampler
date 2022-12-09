<?php
	plugin_header();
	
	$m      =   ($PAGE == 'sampler_mono') ? 'm' : 's';
	$im     =   ($m == 'm') ? ' mono' : ' stereo';
?>

<p>This plugin implements single-note MIDI sample player with<?= $im ?> input and<?= $im ?> output.
There are up to eight samples available to play for different note velocities.</p>

<p><b>Controls:</b></p>
<ul>
	<li><b>Bypass</b> - hot bypass switch, when turned on (led indicator is shining), the plugin does not affect the input signal.</li>
</ul>

<p><b>'MIDI Setup' section:</b></p>
<ul>
	<li><b>Channel</b> - the MIDI channel to trigger notes.</li>
	<li><b>Note</b> - the note and the octave of the note to trigger.</li>
	<li><b>Muting</b> - when enabled, turns off any playback when the Channel Control MIDI message is received.</li>
	<li><b>Note off</b> - when enabled, turns off sample playback for selected channel when the Note Off
	MIDI message is received. The sample fade-out time can be controlled by the corresponding knob. The behaviour is
	different, depending on the	loop settings and muting settings. If the loop is enabled for the sample, then
	receiving MIDI Note OFF event will just break the loop and play the rest of the sample if muting is not set.
	If the instrument muting is set, then it will fade-out the sample.</li>
	<li><b>MIDI #</b> - the MIDI number of the note. Allows to change the number with mouse scroll or mouse double click.</li>
	<li><b>Mute</b> - the button that forces the playback to turn off.</li>
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
	<li><b>Enabled</b> - enables/disables the playback of the corresponding sample.</li>
	<li><b>Active</b> - indicates that the sample is loaded, enabled and ready for playback.</li>
	<li><b>Velocity</b> - the maximum velocity of the note the sample can trigger. Allows to set up velocity layers between different samples.</li>
	<?php if ($m == 'm') { ?>
	<li><b>Gain</b> - the additional gain adjust for the corresponding sample.</li>
	<?php } else { ?>
	<li><b>Pan Left</b> - the panorama of the left audio channel of the corresponding sample.</li>
	<li><b>Pan Right</b> - the panorama of the right audio channel of the corresponding sample.</li>
	<?php } ?>
	<li><b>Listen</b> - the button that forces the playback of the corresponding sample.</li>
	<li><b>Note on</b> - indicates that the playback event of the correponding sample has triggered.</li>
</ul>
<p><b>'Audio channel' section:</b></p>
<ul>
	<li><b>Dynamics</b> - allows to randomize the output gain of the samples.</li>
	<li><b>Time drifting</b> - allows to randomize the time delay between the MIDI Note On event and the start of the sample's playback.</li>
	<li><b>Dry amount</b> - the gain of the input signal passed to the audio inputs of the plugin.</li>
	<li><b>Wet amount</b> - the gain of the processed signal.</li>
	<li><b>Output gain</b> - the overall output gain of the plugin.</li>
</ul>
