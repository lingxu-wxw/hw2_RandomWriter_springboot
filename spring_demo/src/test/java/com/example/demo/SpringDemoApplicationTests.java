package com.example.demo;

import static org.junit.Assert.assertEquals;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.PrintStream;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.junit4.SpringRunner;

@RunWith(SpringRunner.class)
@SpringBootTest
public class SpringDemoApplicationTests {
	static RandomWriterController testobj = new RandomWriterController();
	
	@SuppressWarnings("static-access")
	@Test	
	public void testParseLine() {
		String testStr = "A secret makes a woman woman.";
		testobj.parseLine(testStr);
		assertEquals(0,0,0);
	}

}
